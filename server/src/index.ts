import "dotenv/config";
import http from "http";
import express from "express";
import { Server } from "@colyseus/core";
import { WebSocketTransport } from "@colyseus/ws-transport";
import { Overworld } from "./rooms/Overworld";

const PORT = Number(process.env.PORT || 2567);
const HTTP_PORT = Number(process.env.HTTP_PORT || 3000);

// Express for health / REST
const api = express();
api.get("/health", (_req, res) => res.send("ok"));

// One HTTP server for both Express and WS transport
const httpServer = http.createServer(api);

// Colyseus + WS transport
const gameServer = new Server({
  transport: new WebSocketTransport({ server: httpServer }),
});

// Register your room(s)
gameServer.define("overworld", Overworld);

// Start servers
api.listen(HTTP_PORT, () => console.log(`[api] listening on :${HTTP_PORT}`));
httpServer.listen(PORT, () => console.log(`[realtime] listening on :${PORT}`));

// --- DEV JSON WS gateway (no Colyseus protocol) ---
import { WebSocketServer, WebSocket } from "ws";

type Actor = { id:string, x:number, y:number, mounted:boolean, last:number, spd:number };
const actors = new Map<string, Actor>();
const clients = new Map<WebSocket, string>(); // ws -> actorId

function clampMove(a: Actor, tx: number, ty: number) {
  const now = Date.now();
  const dt = Math.min(0.25, (now - a.last)/1000);
  a.last = now;
  const speed = (a.spd||3.5) * (a.mounted ? 2.4 : 1.0);
  const maxDist = speed * dt;
  const dx = tx - a.x, dy = ty - a.y, d = Math.hypot(dx, dy);
  if (d > 1e-4) {
    const k = Math.min(1, maxDist / d);
    a.x += dx * k; a.y += dy * k;
  }
}

function broadcast(obj: any, except?: WebSocket) {
  const text = JSON.stringify(obj);
  for (const ws of clients.keys()) {
    if (ws !== except && ws.readyState === ws.OPEN) ws.send(text);
  }
}

const wss = new WebSocketServer({ port: 2570 });
console.log("[dev-ws] listening on :2570");

wss.on("connection", (ws) => {
  const id = Math.random().toString(36).slice(2);
  const a: Actor = { id, x: Math.random()*8+1, y: Math.random()*8+1, mounted:false, last: Date.now(), spd:3.5 };
  actors.set(id, a); clients.set(ws, id);

  // tell the new client about themselves + everyone
  ws.send(JSON.stringify({ type:"hello", id, you:a }));
  ws.send(JSON.stringify({ type:"snapshot", actors: Array.from(actors.values()) }));

  // tell everyone else a new actor spawned
  broadcast({ type:"spawn", actor:a }, ws);

  ws.on("message", (buf) => {
    try {
      const msg = JSON.parse(String(buf));
      if (msg.type === "move") clampMove(a, msg.x, msg.y);
      else if (msg.type === "mount") a.mounted = !!msg.mounted;
      else if (msg.type === "cast") {
        const victim = msg.target && actors.get(String(msg.target));
        if (victim && Math.hypot(victim.x - a.x, victim.y - a.y) <= 4.5) {
          broadcast({ type:"damage", src:a.id, dst:victim.id, amt:10 });
        }
      }
    } catch { /* ignore bad packets */ }
  });

  ws.on("close", () => {
    actors.delete(id); clients.delete(ws);
    broadcast({ type:"despawn", id });
  });
});

// broadcast condensed state at 10 Hz
setInterval(() => {
  const payload = JSON.stringify({
    type: "state",
    actors: Array.from(actors.values()).map(({id,x,y,mounted}) => ({id,x,y,mounted}))
  });
  for (const ws of clients.keys()) {
    if (ws.readyState === ws.OPEN) ws.send(payload);
  }
}, 100);
