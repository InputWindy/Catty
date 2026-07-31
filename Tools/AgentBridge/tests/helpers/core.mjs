import { randomUUID } from "node:crypto";
import { CommandExecutor } from "../../src/execution/command-executor.mjs";
import { NullAuditLog } from "../../src/logging/audit-log.mjs";
import { SessionManager } from "../../src/sessions/session-manager.mjs";
import { createDefaultToolRegistry } from "../../src/tools/definitions.mjs";
import {
  WorldAdapterFactory,
  worldAdapterConfigDefaults,
} from "../../src/world/world-adapter-factory.mjs";

export function createTestCore({ audit_log = new NullAuditLog() } = {}) {
  const tool_registry = createDefaultToolRegistry();
  const world_adapter_factory = new WorldAdapterFactory({
    config: { ...worldAdapterConfigDefaults },
    tool_registry,
  });
  const session_manager = new SessionManager({
    world_adapter_factory,
  });
  const command_executor = new CommandExecutor({
    session_manager,
    tool_registry,
    audit_log,
  });
  const session = session_manager.createSession();
  const adapter = session.adapter;
  return {
    session_manager,
    tool_registry,
    world_adapter_factory,
    command_executor,
    session,
    adapter,
    world: adapter.mock_world,
    undo_journal: adapter.undo_journal,
  };
}

export function toolCall(
  tool_name,
  args,
  expected_revision,
  { dry_run = false } = {}
) {
  return {
    tool_call_id: randomUUID(),
    tool_name,
    expected_revision,
    dry_run,
    args,
  };
}

export async function execute(core, tool_calls, overrides = {}) {
  return core.command_executor.executeBatch({
    protocol_version: "1.0",
    request_id: randomUUID(),
    session_id: core.session.session_id,
    world_id: core.session.world_id,
    tool_calls,
    ...overrides,
  });
}

