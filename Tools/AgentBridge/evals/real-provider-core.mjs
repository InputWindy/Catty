import { randomUUID } from "node:crypto";
import { AgentService } from "../src/agent/agent-service.mjs";
import { resolveProviderConfig } from "../src/agent/provider-config.mjs";
import { ProviderRegistry } from "../src/agent/provider-registry.mjs";
import { CommandExecutor } from "../src/execution/command-executor.mjs";
import { SessionManager } from "../src/sessions/session-manager.mjs";
import { createDefaultToolRegistry } from "../src/tools/definitions.mjs";
import {
  WorldAdapterFactory,
  worldAdapterConfigDefaults,
} from "../src/world/world-adapter-factory.mjs";

export class MemoryAuditLog {
  constructor() {
    this.records = [];
  }

  async write(record) {
    this.records.push(structuredClone(record));
  }

  usage() {
    const provider_records = this.records.filter((record) =>
      ["plan", "finalize"].includes(record.request_phase)
    );
    const sum = (field) => {
      const values = provider_records
        .map((record) => record[field])
        .filter((value) => Number.isSafeInteger(value));
      return values.length > 0
        ? values.reduce((total, value) => total + value, 0)
        : null;
    };
    return {
      input_tokens: sum("input_tokens"),
      output_tokens: sum("output_tokens"),
      total_tokens: sum("total_tokens"),
      cached_input_tokens: sum("cached_input_tokens"),
    };
  }
}

export async function createRealProviderCore({
  env = process.env,
  cwd = process.cwd(),
} = {}) {
  const config = resolveProviderConfig({
    env: {
      ...env,
      CATTY_AGENT_MOCK: "0",
      CATTY_AI_PROVIDER: "deepseek",
    },
    cwd,
  });
  const provider_registry = new ProviderRegistry({ config });
  const provider = await provider_registry.initialize();
  const tool_registry = createDefaultToolRegistry();
  const world_adapter_factory = new WorldAdapterFactory({
    config: { ...worldAdapterConfigDefaults },
    tool_registry,
  });
  const session_manager = new SessionManager({
    world_adapter_factory,
  });
  const audit_log = new MemoryAuditLog();
  const command_executor = new CommandExecutor({
    session_manager,
    tool_registry,
    audit_log,
  });
  const agent_service = new AgentService({
    session_manager,
    tool_registry,
    command_executor,
    provider,
    audit_log,
  });
  return {
    config,
    provider_registry,
    provider,
    session_manager,
    tool_registry,
    world_adapter_factory,
    audit_log,
    command_executor,
    agent_service,
    createSession(initial_entities = []) {
      const session = session_manager.createSession();
      for (const entity of initial_entities) {
        session.adapter.mock_world.spawnPrimitive(entity);
      }
      return session;
    },
    async run(session, message) {
      const record_start = audit_log.records.length;
      const snapshot = await session.adapter.getSnapshot({
        request_id: randomUUID(),
        session_id: session.session_id,
        world_id: session.world_id,
      });
      const result = await agent_service.run({
        protocol_version: "1.0",
        request_id: randomUUID(),
        session_id: session.session_id,
        message,
        expected_revision: snapshot.revision,
      });
      const records = audit_log.records.slice(record_start);
      const plan_record = records.find(
        (record) => record.request_phase === "plan"
      );
      return {
        result,
        plan_tool_names: (plan_record?.tool_calls || []).map(
          (tool_call) => tool_call.tool_name
        ),
        records,
      };
    },
    async close() {
      await provider_registry.close();
      await session_manager.close();
    },
    async getSnapshot(session) {
      return session.adapter.getSnapshot({
        request_id: randomUUID(),
        session_id: session.session_id,
        world_id: session.world_id,
      });
    },
  };
}

export function hasDeepSeekKey(env = process.env) {
  return Boolean(
    String(env.CATTY_AI_API_KEY || "").trim() ||
      String(env.DEEPSEEK_API_KEY || "").trim()
  );
}
