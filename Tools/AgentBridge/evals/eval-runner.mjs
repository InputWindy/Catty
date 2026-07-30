import fs from "node:fs/promises";
import path from "node:path";
import { randomUUID } from "node:crypto";
import { AgentService } from "../src/agent/agent-service.mjs";
import { MockProvider } from "../src/agent/providers/mock-provider.mjs";
import { CommandExecutor } from "../src/execution/command-executor.mjs";
import { UndoJournal } from "../src/history/undo-journal.mjs";
import { NullAuditLog } from "../src/logging/audit-log.mjs";
import { SessionManager } from "../src/sessions/session-manager.mjs";
import { createDefaultToolRegistry } from "../src/tools/definitions.mjs";
import { assertFinal, assertTurn } from "./assertions.mjs";

const DEFAULT_CASES_DIRECTORY = new URL("./cases/", import.meta.url);

class RecordingMockProvider {
  constructor() {
    this.name = "mock";
    this.provider = new MockProvider();
    this.last_output = null;
  }

  async run(input) {
    this.last_output = await this.provider.run(input);
    return this.last_output;
  }
}

function createScenarioCore(initial_entities = []) {
  const session_manager = new SessionManager();
  const tool_registry = createDefaultToolRegistry();
  const undo_journal = new UndoJournal();
  const audit_log = new NullAuditLog();
  const command_executor = new CommandExecutor({
    session_manager,
    tool_registry,
    undo_journal,
    audit_log,
  });
  const provider = new RecordingMockProvider();
  const agent_service = new AgentService({
    session_manager,
    tool_registry,
    command_executor,
    provider,
    audit_log,
  });
  const session = session_manager.createSession();
  for (const entity of initial_entities) {
    session.world.spawnPrimitive(entity);
  }
  return { agent_service, provider, session };
}

async function loadCaseFile(file_path) {
  const source = await fs.readFile(file_path, "utf8");
  const document = JSON.parse(source);
  if (!Array.isArray(document.cases)) {
    throw new Error(`Eval file must contain a cases array: ${file_path}`);
  }
  return document.cases;
}

async function defaultCaseFiles() {
  const entries = await fs.readdir(DEFAULT_CASES_DIRECTORY, {
    withFileTypes: true,
  });
  return entries
    .filter((entry) => entry.isFile() && entry.name.endsWith(".json"))
    .map((entry) => new URL(entry.name, DEFAULT_CASES_DIRECTORY))
    .sort((left, right) => left.pathname.localeCompare(right.pathname));
}

function displayPath(file_path) {
  return file_path instanceof URL ? file_path.pathname : path.resolve(file_path);
}

export async function runEvaluations({
  case_files,
  output = process.stdout,
} = {}) {
  const started_at = performance.now();
  const files = case_files?.length ? case_files : await defaultCaseFiles();
  const scenarios = [];
  for (const file_path of files) {
    const loaded = await loadCaseFile(file_path);
    for (const scenario of loaded) {
      scenarios.push({ ...scenario, file_path: displayPath(file_path) });
    }
  }

  let passed = 0;
  let failed = 0;
  for (const scenario of scenarios) {
    const core = createScenarioCore(scenario.initial_entities);
    let scenario_failed = false;
    for (const [index, turn] of scenario.turns.entries()) {
      const before_snapshot = core.session.world.snapshot();
      try {
        const result = await core.agent_service.run({
          protocol_version: "1.0",
          request_id: randomUUID(),
          session_id: core.session.session_id,
          message: turn.message,
          expected_revision: before_snapshot.revision,
        });
        const after_snapshot = core.session.world.snapshot();
        assertTurn({
          expectation: turn.expect || {},
          result,
          provider_output: core.provider.last_output,
          before_snapshot,
          after_snapshot,
        });
      } catch (error) {
        failed += 1;
        scenario_failed = true;
        output.write(
          `FAIL ${scenario.name} (turn ${index + 1}: ${JSON.stringify(turn.message)})\n`
        );
        output.write(`  source: ${scenario.file_path}\n`);
        output.write(`  ${error.message.replaceAll("\n", "\n  ")}\n`);
        break;
      }
    }
    if (scenario_failed) {
      continue;
    }
    try {
      assertFinal(scenario.final, core.session.world.snapshot());
      passed += 1;
      output.write(`PASS ${scenario.name}\n`);
    } catch (error) {
      failed += 1;
      output.write(`FAIL ${scenario.name} (final state)\n`);
      output.write(`  source: ${scenario.file_path}\n`);
      output.write(`  ${error.message.replaceAll("\n", "\n  ")}\n`);
    }
  }

  const duration_ms = Math.round(performance.now() - started_at);
  output.write(
    `\nEval summary: ${passed} passed, ${failed} failed, ${duration_ms} ms\n`
  );
  return {
    passed,
    failed,
    total: passed + failed,
    duration_ms,
  };
}
