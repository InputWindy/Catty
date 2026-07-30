import { randomUUID } from "node:crypto";
import { AgentError } from "../protocol/errors.mjs";
import { MockWorld } from "../world/mock-world.mjs";

export class SessionManager {
  constructor() {
    this.sessions = new Map();
  }

  createSession({ session_id = randomUUID(), world_id = randomUUID() } = {}) {
    const session = {
      session_id,
      world: new MockWorld({ world_id }),
      request_results: new Map(),
      request_promises: new Map(),
      agent_request_results: new Map(),
      agent_request_promises: new Map(),
      busy: false,
      created_at_ms: Date.now(),
    };
    this.sessions.set(session_id, session);
    return session;
  }

  getSession(session_id) {
    const session = this.sessions.get(session_id);
    if (!session) {
      throw new AgentError(
        "UNKNOWN_SESSION",
        `Unknown session: ${session_id}`,
        { session_id }
      );
    }
    return session;
  }

  getWorld(session_id, world_id) {
    const session = this.getSession(session_id);
    if (session.world.world_id !== world_id) {
      throw new AgentError("UNKNOWN_WORLD", `Unknown world: ${world_id}`, {
        session_id,
        world_id,
      });
    }
    return session.world;
  }
}
