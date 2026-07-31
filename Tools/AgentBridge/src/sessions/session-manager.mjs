import { randomUUID } from "node:crypto";
import { AgentError } from "../protocol/errors.mjs";
import { createDefaultToolRegistry } from "../tools/definitions.mjs";
import {
  WorldAdapterFactory,
  worldAdapterConfigDefaults,
} from "../world/world-adapter-factory.mjs";
import { createEntityContext } from "./entity-context.mjs";

export class SessionManager {
  constructor({ world_adapter_factory } = {}) {
    this.sessions = new Map();
    this.world_adapter_factory =
      world_adapter_factory ||
      new WorldAdapterFactory({
        config: { ...worldAdapterConfigDefaults },
        tool_registry: createDefaultToolRegistry(),
      });
  }

  createSession({ session_id = randomUUID(), world_id = randomUUID() } = {}) {
    const adapter = this.world_adapter_factory.createForSession({
      session_id,
      world_id,
    });
    const session = {
      session_id,
      world_id,
      adapter,
      last_world_revision: 0,
      request_results: new Map(),
      request_promises: new Map(),
      agent_request_results: new Map(),
      agent_request_promises: new Map(),
      entity_context: createEntityContext(),
      normalized_messages: [],
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

  getAdapter(session_id, world_id) {
    const session = this.getSession(session_id);
    if (session.world_id !== world_id) {
      throw new AgentError("UNKNOWN_WORLD", `Unknown world: ${world_id}`, {
        session_id,
        world_id,
      });
    }
    return session.adapter;
  }

  async deleteSession(session_id) {
    const session = this.sessions.get(session_id);
    if (!session) {
      return false;
    }
    this.sessions.delete(session_id);
    await this.world_adapter_factory.release(session.adapter);
    return true;
  }

  async close() {
    const sessions = [...this.sessions.values()];
    this.sessions.clear();
    await Promise.allSettled(
      sessions.map((session) =>
        this.world_adapter_factory.release(session.adapter)
      )
    );
    await this.world_adapter_factory.close();
  }
}
