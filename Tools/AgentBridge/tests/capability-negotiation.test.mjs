import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import test from "node:test";
import { AgentService } from "../src/agent/agent-service.mjs";
import {
  DEFAULT_PROVIDER_CAPABILITIES,
  createProviderOutput,
} from "../src/agent/provider-contract.mjs";
import { MockProvider } from "../src/agent/providers/mock-provider.mjs";
import { OpenAICompatibleProvider } from "../src/agent/providers/openai-compatible-provider.mjs";
import {
  createTestCore,
  execute,
  toolCall,
} from "./helpers/core.mjs";
import {
  completion,
  startFakeOpenAIServer,
} from "./helpers/fake-openai-server.mjs";

const MINIMAL_TOOL_NAMES = [
  "world.get_summary",
  "entity.spawn_primitive",
  "entity.set_transform",
];

function useMinimalCapabilities(core) {
  core.adapter.capabilities = Object.freeze({
    supports_atomic_transactions: false,
    supports_dry_run: false,
    supports_undo: false,
    supports_idempotency: true,
    max_tool_calls: 1,
    supported_tools: Object.freeze([...MINIMAL_TOOL_NAMES]),
  });
  return core;
}

function createService(core, provider, audit_log = { write: async () => {} }) {
  return new AgentService({
    session_manager: core.session_manager,
    tool_registry: core.tool_registry,
    command_executor: core.command_executor,
    provider,
    audit_log,
  });
}

function runAgent(core, service, message) {
  return service.run({
    protocol_version: "1.0",
    request_id: randomUUID(),
    session_id: core.session.session_id,
    message,
    expected_revision: core.world.revision,
  });
}

test("AgentService gives Mock and real provider implementations the same stable tool subset", async (t) => {
  const mock_core = useMinimalCapabilities(createTestCore());
  const mock_provider = new MockProvider();
  let mock_tool_names;
  const mock_plan = mock_provider.plan.bind(mock_provider);
  mock_provider.plan = async (input) => {
    mock_tool_names = input.tool_definitions.map((tool) => tool.name);
    return mock_plan(input);
  };
  const mock_result = await runAgent(
    mock_core,
    createService(mock_core, mock_provider),
    "create a cube"
  );

  const provider_core = useMinimalCapabilities(createTestCore());
  let provider_tool_names;
  const provider = {
    name: "recording-provider",
    model: "recording-model",
    max_tool_calls: 16,
    capabilities: { ...DEFAULT_PROVIDER_CAPABILITIES },
    async plan(input) {
      provider_tool_names = input.tool_definitions.map((tool) => tool.name);
      return createProviderOutput({
        provider: this.name,
        model: this.model,
        assistant_message: "attempt unsupported tool",
        tool_calls: [
          {
            tool_call_id: randomUUID(),
            tool_name: "entity.destroy",
            args: { entity_id: randomUUID() },
          },
        ],
        finish_reason: "tool_calls",
        provider_metadata: {
          phase: "plan",
          attempt_count: 1,
          duration_ms: 0,
          http_status: null,
        },
      });
    },
  };
  let execute_calls = 0;
  const execute_transaction = provider_core.adapter.executeTransaction.bind(
    provider_core.adapter
  );
  provider_core.adapter.executeTransaction = async (input) => {
    execute_calls += 1;
    return execute_transaction(input);
  };
  const provider_result = await runAgent(
    provider_core,
    createService(provider_core, provider),
    "use an unsupported tool"
  );

  const fake_provider = await startFakeOpenAIServer(() => ({
    body: completion({ content: "minimal tools received" }),
  }));
  t.after(() => fake_provider.close());
  const real_core = useMinimalCapabilities(createTestCore());
  const real_provider = new OpenAICompatibleProvider({
    provider_id: "openai-compatible",
    base_url: fake_provider.base_url,
    model: "test-model",
    api_key: "test-key",
    max_retries: 0,
    timeout_ms: 1_000,
  });
  t.after(() => real_provider.close());
  const real_result = await runAgent(
    real_core,
    createService(real_core, real_provider),
    "describe available actions"
  );

  assert.equal(mock_result.ok, true);
  assert.deepEqual(mock_tool_names, MINIMAL_TOOL_NAMES);
  assert.deepEqual(provider_tool_names, MINIMAL_TOOL_NAMES);
  assert.equal(provider_result.ok, false);
  assert.equal(provider_result.error.code, "INVALID_REQUEST");
  assert.equal(execute_calls, 0);
  assert.equal(provider_core.world.revision, 0);
  assert.equal(real_result.ok, true);
  assert.deepEqual(
    fake_provider.requests[0].body.tools.map((tool) => tool.function.name),
    [
      "world__get_summary",
      "entity__spawn_primitive",
      "entity__set_transform",
    ]
  );
});

test("CommandExecutor rejects minimal-profile violations before adapter calls", async () => {
  const records = [];
  const core = useMinimalCapabilities(
    createTestCore({
      audit_log: { write: async (record) => records.push(record) },
    })
  );
  const before_context = structuredClone(core.session.entity_context);
  let execute_calls = 0;
  let undo_calls = 0;
  let last_execute_input;
  const execute_transaction = core.adapter.executeTransaction.bind(
    core.adapter
  );
  const undo = core.adapter.undo.bind(core.adapter);
  core.adapter.executeTransaction = async (input) => {
    execute_calls += 1;
    last_execute_input = structuredClone({ ...input, signal: undefined });
    return execute_transaction(input);
  };
  core.adapter.undo = async (input) => {
    undo_calls += 1;
    return undo(input);
  };

  const summary = await execute(core, [
    toolCall("world.get_summary", {}, 0),
  ]);
  const unsupported = await execute(core, [
    toolCall("entity.destroy", { entity_id: randomUUID() }, 0),
  ]);
  const too_many = await execute(core, [
    toolCall("world.get_summary", {}, 0),
    toolCall("entity.spawn_primitive", { primitive_type: "cube" }, 0),
  ]);
  const dry_run = await execute(core, [
    toolCall("world.get_summary", {}, 0, { dry_run: true }),
  ]);
  const undo_result = await execute(core, [
    toolCall("history.undo", {}, 0),
  ]);

  assert.equal(summary.ok, true);
  assert.equal(last_execute_input.atomic, false);
  assert.equal(unsupported.error.code, "INVALID_REQUEST");
  assert.equal(too_many.error.code, "INVALID_REQUEST");
  assert.equal(dry_run.error.code, "INVALID_REQUEST");
  assert.equal(undo_result.error.code, "UNDO_NOT_AVAILABLE");
  assert.equal(execute_calls, 1);
  assert.equal(undo_calls, 0);
  assert.equal(core.world.revision, 0);
  assert.deepEqual(core.session.entity_context, before_context);
  for (const rejection of [unsupported, too_many, dry_run, undo_result]) {
    assert.equal(rejection.world_revision, 0);
    assert.equal(rejection.undo_token, null);
  }
  assert.equal(records.length, 5);
  for (const record of records) {
    assert.deepEqual(record.adapter_supported_tools, MINIMAL_TOOL_NAMES);
    assert.equal(record.adapter_max_tool_calls, 1);
    assert.equal(record.adapter_supports_atomic, false);
    assert.equal(record.adapter_supports_dry_run, false);
    assert.equal(record.adapter_supports_undo, false);
  }
  assert.deepEqual(
    records.map((record) => record.capability_rejection_reason),
    [null, "capability_insufficient", "capability_insufficient", "capability_insufficient", "undo_not_available"]
  );
});
