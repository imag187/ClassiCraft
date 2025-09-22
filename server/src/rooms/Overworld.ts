// src/rooms/Overworld.ts
import { Room, Client } from "@colyseus/core";
import { Schema, type, MapSchema } from "@colyseus/schema";

class Statuses extends Schema {
  @type("boolean") rooted: boolean = false;
  @type("number") rootUntil: number = 0;
  @type("boolean") stunned: boolean = false;
  @type("number") stunUntil: number = 0;
  @type("boolean") mounted: boolean = false;
}

class Actor extends Schema {
  @type("string") id: string = "";
  @type("number") x: number = 0;
  @type("number") y: number = 0;
  @type("string") target: string = "";
  @type("number") gcdUntil: number = 0;     // ms epoch
  @type(Statuses) statuses: Statuses = new Statuses();
  @type("number") baseSpeed: number = 3.5;  // tiles/sec
  @type("number") speedMult: number = 1.0;  // buffs (sprint, etc.)
  @type("number") lastMoveAt: number = 0;   // ms
}

class WorldState extends Schema {
  @type({ map: Actor }) actors = new MapSchema<Actor>();
}

type MoveMsg = { x:number, y:number };
type CastMsg = { ability:string, target?:string };
type MountMsg = { mounted:boolean };

export class Overworld extends Room<WorldState> {
  maxClients = 120;

  onCreate() {
    this.setState(new WorldState());
    this.onMessage("move", (client, d:MoveMsg) => this.handleMove(client, d));
    this.onMessage("cast", (client, d:CastMsg) => this.handleCast(client, d));
    this.onMessage("mount", (client, d:MountMsg) => this.handleMount(client, d));
  }

  onJoin(client: Client) {
    const a = new Actor();
    a.id = client.sessionId;
    a.x = Math.random() * 8 + 1;
    a.y = Math.random() * 8 + 1;
    a.lastMoveAt = Date.now();
    this.state.actors.set(client.sessionId, a);
    client.send("hello", { id: client.sessionId });
  }

  onLeave(client: Client) {
    this.state.actors.delete(client.sessionId);
  }

  private handleMove(client: Client, d: MoveMsg) {
    const a = this.state.actors.get(client.sessionId);
    if (!a) return;
    const now = Date.now();
    const dt = Math.max(0, Math.min(0.25, (now - a.lastMoveAt) / 1000)); // clamp 250ms/frame
    a.lastMoveAt = now;

    if (a.statuses.rooted && now < a.statuses.rootUntil) return;
    if (a.statuses.stunned && now < a.statuses.stunUntil) return;

    const speed = a.baseSpeed * a.speedMult * (a.statuses.mounted ? 2.4 : 1.0);
    const maxDist = speed * dt;

    const dx = d.x - a.x, dy = d.y - a.y;
    const dist = Math.hypot(dx, dy); if (dist <= 1e-4) return;
    const k = Math.min(1, maxDist / dist);
    a.x += dx * k; a.y += dy * k;
  }

  private handleCast(client: Client, d: CastMsg) {
    const a = this.state.actors.get(client.sessionId); if (!a) return;
    const now = Date.now(); if (now < a.gcdUntil) return;
    a.gcdUntil = now + 1100; // 1.1s GCD

    if (d.target) {
      const t = this.state.actors.get(d.target);
      if (t) {
        const inRange = Math.hypot(t.x - a.x, t.y - a.y) <= 4.5;
        if (inRange) this.broadcast("damage", { src:a.id, dst:t.id, amt:10 });
      }
    }
  }

  private handleMount(client: Client, d: MountMsg) {
    const a = this.state.actors.get(client.sessionId); if (!a) return;
    a.statuses.mounted = !!d.mounted;
  }
}
