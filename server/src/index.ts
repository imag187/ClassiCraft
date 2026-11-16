import "dotenv/config";
import http from "http";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
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

// The playtest uses the ground layer of the first placed Studio map.
type WorldObject = { x: number; y: number; z: number; kind: string; name: string; collision: boolean; artwork?: string };
type Cell = { x: number; y: number; z: number; tile: string; collision: boolean };
type WorldProject = {
  tiles: { id: string; pixels: number[] }[];
  maps: { id: string; name: string; width: number; height: number; cells: Cell[] }[];
  world: { maps: { map: string; x: number; y: number; z: number }[]; objects: WorldObject[] };
};

const project: WorldProject = JSON.parse(readFileSync(
  process.env.CLASSICRAFT_WORLD_PATH || resolve(__dirname, "../../worlds/test-world/world-project.json"), "utf8",
));
const placement = project.world.maps[0];
const map = project.maps.find((entry) => entry.id === placement?.map);
if (!map) throw new Error("The Studio world needs a placed map.");
const objects = project.world.objects.filter((object) => object.z >= placement.z);
const blocked = new Set<string>();
const dungeon = {
  name: map.name,
  width: map.width,
  height: map.height,
  objects: objects.map((object, index) => ({
    id: `object-${index}`,
    ...object, x: object.x - placement.x, y: object.y - placement.y, z: object.z - placement.z,
  })),
};
const ground = map.cells.filter((cell) => cell.z === 0);
const worldContent = JSON.stringify({
  tiles: project.tiles.map(({ id, pixels }) => ({ id, pixels })),
  cells: map.cells,
});
const contentVersion = createHash("sha256").update(worldContent).digest("hex");
const contentMessage = JSON.stringify({ type: "world_content", version: contentVersion, content: worldContent });

for (const cell of [...ground, ...dungeon.objects.filter((object) => object.z === 0)]) {
  if (cell.collision) blocked.add(`${cell.x},${cell.y}`);
}

const spawn = dungeon.objects.find((object) => object.kind === "spawn" && object.z === 0);
if (!spawn || !canStand(spawn.x + 0.5, spawn.y + 0.5)) {
  throw new Error("The Studio world needs a spawn on an open tile.");
}

function canStand(x: number, y: number) {
  return x >= 0.2 && y >= 0.2 && x <= dungeon.width - 0.2 && y <= dungeon.height - 0.2
    && !blocked.has(`${Math.floor(x)},${Math.floor(y)}`);
}

type Actor = {
  id: string;
  x: number;
  y: number;
  mounted: boolean;
  moveX: number;
  moveY: number;
  inputUntil: number;
};
const actors = new Map<string, Actor>();
const collectedItems = new Set<string>();
const clients = new Map<WebSocket, string>(); // ws -> actorId

function advanceActor(actor: Actor, dt: number, now: number) {
  if (now >= actor.inputUntil || (!actor.moveX && !actor.moveY)) return;
  const distance = 3.5 * (actor.mounted ? 2.4 : 1) * dt;
  const steps = Math.max(1, Math.ceil(distance / 0.1));
  const dx = actor.moveX * distance / steps;
  const dy = actor.moveY * distance / steps;
  for (let step = 0; step < steps; step++) {
    // Check each axis so holding a diagonal along a wall still allows sliding.
    if (canStand(actor.x + dx, actor.y)) actor.x += dx;
    if (canStand(actor.x, actor.y + dy)) actor.y += dy;
  }
}

function broadcast(obj: any, except?: WebSocket) {
  const text = JSON.stringify(obj);
  for (const ws of clients.keys()) {
    if (ws !== except && ws.readyState === ws.OPEN) ws.send(text);
  }
}

const dungeonHost = process.env.CLASSICRAFT_WS_HOST || "127.0.0.1";
const wss = new WebSocketServer({ host: dungeonHost, port: 2570 }, () => {
  console.log(`[dev-ws] listening on ${dungeonHost}:2570`);
});

wss.on("connection", (ws) => {
  const id = Math.random().toString(36).slice(2);
  const a: Actor = { id, x: spawn.x + 0.5, y: spawn.y + 0.5, mounted: false, moveX: 0, moveY: 0, inputUntil: 0 };
  actors.set(id, a); clients.set(ws, id);

  // tell the new client about themselves + everyone
  ws.send(JSON.stringify({ type:"hello", id, you:a }));
  ws.send(JSON.stringify({ type:"snapshot", contentVersion, dungeon: {
    ...dungeon, objects: dungeon.objects.filter((object) => !collectedItems.has(object.id)),
  }, actors: Array.from(actors.values()) }));

  // tell everyone else a new actor spawned
  broadcast({ type:"spawn", actor:a }, ws);

  let contentSent = false;
  ws.on("message", (buf) => {
    try {
      const msg = JSON.parse(String(buf));
      if (msg.type === "move_input" && Number.isFinite(msg.x) && Number.isFinite(msg.y)) {
        const x = Math.max(-1, Math.min(1, msg.x));
        const y = Math.max(-1, Math.min(1, msg.y));
        const magnitude = Math.max(1, Math.hypot(x, y));
        a.moveX = x / magnitude;
        a.moveY = y / magnitude;
        a.inputUntil = Date.now() + 350;
      }
      else if (msg.type === "get_world_content" && msg.version === contentVersion && !contentSent) {
        contentSent = true;
        ws.send(contentMessage);
      }
      else if (msg.type === "mount") a.mounted = !!msg.mounted;
      else if (msg.type === "pickup") {
        const item = dungeon.objects.find((object) => object.kind === "item" && object.z === 0 && object.id === msg.id);
        if (item && !collectedItems.has(item.id) && Math.hypot(item.x + 0.5 - a.x, item.y + 0.5 - a.y) <= 1.5) {
          collectedItems.add(item.id);
          broadcast({ type: "item_removed", id: item.id });
          ws.send(JSON.stringify({ type: "item_collected", name: item.name, artwork: item.artwork }));
        }
      }
      else if (msg.type === "cast") {
        const dummy = dungeon.objects.find((object) => object.kind === "enemy" && object.z === 0 && object.id === msg.target);
        const victim = dummy
          ? { id: msg.target, x: dummy.x + 0.5, y: dummy.y + 0.5 }
          : actors.get(String(msg.target));
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

// Simulate movement and publish authoritative positions at 20 Hz.
let lastTick = Date.now();
setInterval(() => {
  const now = Date.now();
  const dt = Math.min(0.1, (now - lastTick) / 1000);
  lastTick = now;
  for (const actor of actors.values()) advanceActor(actor, dt, now);
  const payload = JSON.stringify({
    type: "state",
    actors: Array.from(actors.values()).map(({id,x,y,mounted}) => ({id,x,y,mounted}))
  });
  for (const ws of clients.keys()) {
    if (ws.readyState === ws.OPEN) ws.send(payload);
  }
}, 50);
