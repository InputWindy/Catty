import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import test from "node:test";
import { AgentService } from "../src/agent/agent-service.mjs";
import {
  DEFAULT_PROVIDER_CAPABILITIES,
  createProviderOutput,
} from "../src/agent/provider-contract.mjs";
import {
  ProviderError,
  providerErrorReasons,
} from "../src/agent/providers/provider-errors.mjs";
import { createTestCore } from "./helpers/core.mjs";

function output({
  assistant_message = "planned",
  tool_calls = [],
  phase = "plan",
} = {}) {
  return createProviderOutput({
    provider: "test",
    model: "test-model",
    assistant_message,
    tool_calls,
    finish_reason: tool_calls.length ? "tool_calls" : "stop",
    provider_metadata: {
      phase,
      attempt_count: 1,
      duration_ms: 1,
      http_status: 200,
    },
  });
}

function provider({ plan, finalize, supports_finalization = false }) {
  return {
    name: "test",
    model: "test-model",
    max_tool_calls: 16,
    finalization_enabled: supports_finalization,
    capabilities: {
      ...DEFAULT_PROVIDER_CAPABILITIES,
      supports_finalization,
    },
    plan,
    finalize,
    getMetadata() {
      return {
        provider: this.name,
        model: this.model,
        ready: true,
      };
    },
  };
}

function harness(selected_provider) {
  const records = [];
  const core = createTestCore({
    audit_log: { write: async (record) => records.push(record) },
  });
  const service = new AgentService({
    session_manager: core.session_manager,
    tool_registry: core.tool_registry,
    command_executor: core.command_executor,
    provider: selected_provider,
    audit_log: { write: async (record) => records.push(record) },
  });
  return {
    ...core,
    records,
    service,
    run(message = "create a cube", overrides = {}) {
      return service.run({
        protocol_version: "1.0",
        request_id: randomUUID(),
        session_id: core.session.session_id,
        message,
        expected_revision: core.world.revision,
        ...overrides,
      });
    },
  };
}

function spawnCall(args = { primitive_type: "cube" }) {
  return {
    tool_call_id: randomUUID(),
    tool_name: "entity.spawn_primitive",
    args,
  };
}

test("AgentService sends the unified plan input and executes only through CommandExecutor", async () => {
  let plan_input;
  const call = spawnCall();
  const core = harness(
    provider({
      plan: async (input) => {
        plan_input = input;
        return output({ tool_calls: [call] });
      },
    })
  );
  const result = await core.run();

  assert.equal(plan_input.request_id.length, 36);
  assert.equal(plan_input.session_id, core.session.session_id);
  assert.equal(plan_input.user_message, "create a cube");
  assert.equal(plan_input.normalized_messages[0].role, "system");
  assert.equal(plan_input.tool_definitions.length, 8);
  assert.equal(result.ok, true);
  assert.equal(result.world_revision, 1);
  assert.equal(core.world.entities.size, 1);
  assert.equal(core.undo_journal.records.size, 1);
});

test("plan Provider failure leaves world, revision, and UndoJournal unchanged", async () => {
  const core = harness(
    provider({
      plan: async () => {
        throw new ProviderError(
          providerErrorReasons.CONNECTION_FAILED,
          "Provider connection failed",
          { provider: "test", model: "test-model" }
        );
      },
    })
  );
  const before = core.world.snapshot();
  const result = await core.run();

  assert.equal(result.ok, false);
  assert.equal(result.error.code, "EXECUTION_FAILED");
  assert.deepEqual(core.world.snapshot(), before);
  assert.equal(core.undo_journal.records.size, 0);
});

test("successful tools are finalized exactly once with real ToolResults", async () => {
  let finalize_calls = 0;
  let finalize_input;
  const call = spawnCall();
  const core = harness(
    provider({
      supports_finalization: true,
      plan: async () => output({ tool_calls: [call] }),
      finalize: async (input) => {
        finalize_calls += 1;
        finalize_input = input;
        return output({
          assistant_message: "finalized success",
          phase: "finalize",
        });
      },
    })
  );
  const result = await core.run();

  assert.equal(finalize_calls, 1);
  assert.equal(result.assistant_message, "finalized success");
  const tool_message = finalize_input.normalized_messages.find(
    (message) => message.role === "tool"
  );
  assert.equal(JSON.parse(tool_message.content).ok, true);
  assert.equal(core.world.revision, 1);
});

test("finalization failure keeps successful mutation and uses deterministic fallback", async () => {
  let plan_calls = 0;
  let finalize_calls = 0;
  const core = harness(
    provider({
      supports_finalization: true,
      plan: async () => {
        plan_calls += 1;
        return output({ tool_calls: [spawnCall()] });
      },
      finalize: async () => {
        finalize_calls += 1;
        throw new ProviderError(
          providerErrorReasons.CONNECTION_FAILED,
          "finalization failed",
          { provider: "test", model: "test-model", phase: "finalize" }
        );
      },
    })
  );
  const result = await core.run();

  assert.equal(result.ok, true);
  assert.equal(plan_calls, 1);
  assert.equal(finalize_calls, 1);
  assert.equal(core.world.entities.size, 1);
  assert.equal(core.world.revision, 1);
  assert.match(result.assistant_message, /CommandExecutor/);
  assert.equal(
    core.records.some(
      (record) =>
        record.request_phase === "finalize" &&
        record.finalization_failed === true
    ),
    true
  );
});

test("failed tools are finalized with failure but Provider text cannot claim success", async () => {
  let finalize_input;
  const core = harness(
    provider({
      supports_finalization: true,
      plan: async () =>
        output({
          assistant_message: "success",
          tool_calls: [
            {
              tool_call_id: randomUUID(),
              tool_name: "entity.destroy",
              args: { entity_id: randomUUID() },
            },
          ],
        }),
      finalize: async (input) => {
        finalize_input = input;
        return output({
          assistant_message: "Everything succeeded.",
          phase: "finalize",
        });
      },
    })
  );
  const result = await core.run("delete missing");

  assert.equal(result.ok, false);
  assert.doesNotMatch(result.assistant_message, /Everything succeeded/);
  assert.match(result.assistant_message, /操作未完成/);
  assert.equal(core.world.revision, 0);
  assert.equal(core.undo_journal.records.size, 0);
  const tool_message = finalize_input.normalized_messages.find(
    (message) => message.role === "tool"
  );
  assert.equal(JSON.parse(tool_message.content).ok, false);
});

test("finalization ToolCalls are rejected without repeating world execution", async () => {
  const core = harness(
    provider({
      supports_finalization: true,
      plan: async () => output({ tool_calls: [spawnCall()] }),
      finalize: async () =>
        output({
          tool_calls: [
            {
              tool_call_id: randomUUID(),
              tool_name: "world.get_summary",
              args: {},
            },
          ],
          phase: "finalize",
        }),
    })
  );
  const result = await core.run();

  assert.equal(result.ok, true);
  assert.equal(core.world.entities.size, 1);
  assert.equal(core.world.revision, 1);
  assert.match(result.assistant_message, /CommandExecutor/);
});
