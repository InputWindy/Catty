import { randomUUID } from "node:crypto";
import { CommandExecutor } from "../../src/execution/command-executor.mjs";
import { UndoJournal } from "../../src/history/undo-journal.mjs";
import { NullAuditLog } from "../../src/logging/audit-log.mjs";
import { SessionManager } from "../../src/sessions/session-manager.mjs";
import { createDefaultToolRegistry } from "../../src/tools/definitions.mjs";

export function createTestCore({ audit_log = new NullAuditLog() } = {}) {
  const session_manager = new SessionManager();
  const tool_registry = createDefaultToolRegistry();
  const undo_journal = new UndoJournal();
  const command_executor = new CommandExecutor({
    session_manager,
    tool_registry,
    undo_journal,
    audit_log,
  });
  const session = session_manager.createSession();
  return {
    session_manager,
    tool_registry,
    undo_journal,
    command_executor,
    session,
    world: session.world,
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
    world_id: core.world.world_id,
    tool_calls,
    ...overrides,
  });
}

