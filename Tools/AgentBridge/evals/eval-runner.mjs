import fs from "node:fs/promises";
import path from "node:path";
import { randomUUID } from "node:crypto";
import { AgentService } from "../src/agent/agent-service.mjs";
import { MockProvider } from "../src/agent/providers/mock-provider.mjs";
import { CommandExecutor } from "../src/execution/command-executor.mjs";
import { NullAuditLog } from "../src/logging/audit-log.mjs";
import { SessionManager } from "../src/sessions/session-manager.mjs";
import { createDefaultToolRegistry } from "../src/tools/definitions.mjs";
import {
  WorldAdapterFactory,
  worldAdapterConfigDefaults,
} from "../src/world/world-adapter-factory.mjs";
import { assertFinal, assertTurn } from "./assertions.mjs";

const DEFAULT_CASES_DIRECTORY = new URL("./cases/", import.meta.url);

class RecordingMockProvider {
  constructor() {
    this.provider = new MockProvider();
    this.name = this.provider.name;
    this.model = this.provider.model;
    this.capabilities = this.provider.capabilities;
    this.max_tool_calls = 16;
    this.last_output = null;
  }

  getMetadata() {
    return this.provider.getMetadata();
  }

  async plan(input) {
    this.last_output = await this.provider.plan(input);
    return this.last_output;
  }

  async close() {
    await this.provider.close();
  }
}

async function getSnapshot(session) {
  const snapshot = await session.adapter.getSnapshot({
    request_id: randomUUID(),
    session_id: session.session_id,
    world_id: session.world_id,
  });
  session.last_world_revision = snapshot.revision;
  return snapshot;
}

async function createScenarioCore(
  initial_entities = [],
  {
    create_world_adapter_factory,
    seed_session,
  } = {}
) {
  const tool_registry = createDefaultToolRegistry();
  const world_adapter_factory = create_world_adapter_factory
    ? await create_world_adapter_factory(tool_registry)
    : new WorldAdapterFactory({
        config: { ...worldAdapterConfigDefaults },
        tool_registry,
      });
  const session_manager = new SessionManager({
    world_adapter_factory,
  });
  const audit_log = new NullAuditLog();
  const command_executor = new CommandExecutor({
    session_manager,
    tool_registry,
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
  await session.adapter.health();
  await getSnapshot(session);
  if (seed_session) {
    await seed_session(session, initial_entities);
  } else {
    for (const entity of initial_entities) {
      session.adapter.mock_world.spawnPrimitive(entity);
    }
  }
  return {
    agent_service,
    provider,
    session,
    session_manager,
    getSnapshot: () => getSnapshot(session),
    async close() {
      await provider.close();
      await session_manager.close();
    },
  };
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
  adapter_name = "mock",
  create_world_adapter_factory,
  seed_session,
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
    const core = await createScenarioCore(scenario.initial_entities, {
      create_world_adapter_factory,
      seed_session,
    });
    let scenario_failed = false;
    try {
      for (const [index, turn] of scenario.turns.entries()) {
        const before_snapshot = await core.getSnapshot();
        try {
          const result = await core.agent_service.run({
            protocol_version: "1.0",
            request_id: randomUUID(),
            session_id: core.session.session_id,
            message: turn.message,
            expected_revision: before_snapshot.revision,
          });
          const after_snapshot = await core.getSnapshot();
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
        assertFinal(scenario.final, await core.getSnapshot());
        passed += 1;
        output.write(`PASS ${scenario.name}\n`);
      } catch (error) {
        failed += 1;
        output.write(`FAIL ${scenario.name} (final state)\n`);
        output.write(`  source: ${scenario.file_path}\n`);
        output.write(`  ${error.message.replaceAll("\n", "\n  ")}\n`);
      }
    } finally {
      await core.close();
    }
  }

  const duration_ms = Math.round(performance.now() - started_at);
  output.write(
    `\nEval summary: adapter=${adapter_name} ${passed} passed, ${failed} failed, ${duration_ms} ms\n`
  );
  return {
    adapter: adapter_name,
    passed,
    failed,
    total: passed + failed,
    duration_ms,
  };
}
