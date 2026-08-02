import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import test from "node:test";
import { AgentService } from "../src/agent/agent-service.mjs";
import {
  DEFAULT_PROVIDER_CAPABILITIES,
  createProviderOutput,
} from "../src/agent/provider-contract.mjs";
import { CommandExecutor } from "../src/execution/command-executor.mjs";
import { SessionManager } from "../src/sessions/session-manager.mjs";
import { createDefaultToolRegistry } from "../src/tools/definitions.mjs";
import { RemoteWorldAdapter } from "../src/world/remote-world-adapter.mjs";
import { WorldAdapterFactory } from "../src/world/world-adapter-factory.mjs";
import { WorldAdapterError } from "../src/world/world-adapter-errors.mjs";
import {
  MINIMAL_WORLD_PROFILE,
  startFakeMahoWorldServer,
} from "./helpers/fake-maho-world-server.mjs";
import {
  postJson,
  startLegacyTestServer,
} from "./helpers/test-server.mjs";

function adapterToolCall(tool_name, args = {}) {
  return {
    tool_call_id: randomUUID(),
    tool_name,
    args,
  };
}

function adapterRequest(identity, expected_revision, tool_calls, overrides = {}) {
  return {
    request_id: randomUUID(),
    session_id: identity.session_id,
    world_id: identity.world_id,
    expected_revision,
    dry_run: false,
    atomic: false,
    tool_calls,
    ...overrides,
  };
}

function executorToolCall(
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

async function createRemoteCore(fake) {
  const tool_registry = createDefaultToolRegistry();
  const world_adapter_factory = new WorldAdapterFactory({
    config: {
      adapter_id: "remote",
      base_url: fake.base_url,
      timeout_ms: 1_000,
      auth_token: "",
      allow_non_loopback: false,
      endpoint_origin: fake.base_url,
      loopback: true,
    },
    tool_registry,
  });
  const session_manager = new SessionManager({ world_adapter_factory });
  const command_executor = new CommandExecutor({
    session_manager,
    tool_registry,
  });
  const session = session_manager.createSession();
  const snapshot = await session.adapter.getSnapshot({
    request_id: randomUUID(),
    session_id: session.session_id,
    world_id: session.world_id,
  });
  session.last_world_revision = snapshot.revision;
  return {
    tool_registry,
    world_adapter_factory,
    session_manager,
    command_executor,
    session,
    adapter: session.adapter,
  };
}

function execute(core, tool_calls, request_id = randomUUID()) {
  return core.command_executor.executeBatch({
    protocol_version: "1.0",
    request_id,
    session_id: core.session.session_id,
    world_id: core.session.world_id,
    tool_calls,
  });
}

function endpointCount(fake, endpoint) {
  return fake.requests.filter(
    (request) => request.pathname === `/world-adapter/v1/${endpoint}`
  ).length;
}

test("Minimal RemoteWorldAdapter negotiates and executes its three-tool profile", async (t) => {
  const fake = await startFakeMahoWorldServer({ profile: "minimal" });
  t.after(() => fake.close());
  const tool_registry = createDefaultToolRegistry();
  const identity = {
    session_id: randomUUID(),
    world_id: randomUUID(),
  };
  const adapter = new RemoteWorldAdapter({
    ...identity,
    tool_registry,
    base_url: fake.base_url,
    timeout_ms: 1_000,
  });
  t.after(() => adapter.close());

  const health = await adapter.health();
  assert.deepEqual(health.capabilities, MINIMAL_WORLD_PROFILE);
  assert.deepEqual(adapter.capabilities, MINIMAL_WORLD_PROFILE);
  assert.deepEqual(
    adapter.getMetadata().capabilities,
    MINIMAL_WORLD_PROFILE
  );

  const summary = await adapter.executeTransaction(
    adapterRequest(identity, 0, [adapterToolCall("world.get_summary")])
  );
  assert.equal(summary.ok, true);
  assert.equal(summary.after_revision, 0);

  const spawn_request = adapterRequest(identity, 0, [
    adapterToolCall("entity.spawn_primitive", {
      primitive_type: "cube",
      name: "MinimalCube",
    }),
  ]);
  const spawned = await adapter.executeTransaction(spawn_request);
  const replayed = await adapter.executeTransaction(spawn_request);
  const entity_id = spawned.tool_results[0].data.entity.entity_id;
  assert.equal(spawned.ok, true);
  assert.equal(spawned.undo_token, null);
  assert.equal(replayed.replayed, true);
  assert.equal(
    replayed.tool_results[0].data.entity.entity_id,
    entity_id
  );

  const transformed = await adapter.executeTransaction(
    adapterRequest(identity, 1, [
      adapterToolCall("entity.set_transform", {
        entity_id,
        transform: { position: [1, 2, 3] },
      }),
    ])
  );
  assert.equal(transformed.ok, true);
  assert.equal(transformed.after_revision, 2);
  const snapshot = await adapter.getSnapshot({
    request_id: randomUUID(),
    ...identity,
  });
  assert.deepEqual(snapshot.entities[0].transform.position, [1, 2, 3]);
  assert.equal(
    fake.requests
      .filter((request) => request.pathname.endsWith("/execute"))
      .every(
        (request) =>
          request.body.atomic === false &&
          request.body.tool_calls.length === 1
      ),
    true
  );
});

test("Minimal CommandExecutor and AgentService reject unsupported requests locally", async (t) => {
  const fake = await startFakeMahoWorldServer({ profile: "minimal" });
  t.after(() => fake.close());
  const core = await createRemoteCore(fake);
  t.after(() => core.session_manager.close());

  const spawned = await execute(core, [
    executorToolCall(
      "entity.spawn_primitive",
      { primitive_type: "sphere" },
      0
    ),
  ]);
  const entity_id = spawned.tool_results[0].data.entity.entity_id;
  assert.equal(spawned.ok, true);
  assert.equal(spawned.undo_token, null);
  assert.equal(core.session.entity_context.last_created_entity_id, entity_id);

  const before_context = structuredClone(core.session.entity_context);
  const before_execute_count = endpointCount(fake, "execute");
  const before_undo_count = endpointCount(fake, "undo");
  const rejected = [];
  rejected.push(
    await execute(core, [
      executorToolCall(
        "entity.set_property",
        { entity_id, property_name: "visible", value: false },
        1
      ),
    ])
  );
  rejected.push(
    await execute(core, [
      executorToolCall("world.get_summary", {}, 1),
      executorToolCall("world.get_summary", {}, 1),
    ])
  );
  rejected.push(
    await execute(core, [
      executorToolCall("world.get_summary", {}, 1, { dry_run: true }),
    ])
  );
  const undo_result = await execute(core, [
    executorToolCall("history.undo", {}, 1),
  ]);
  rejected.push(undo_result);

  let provider_tools;
  const provider = {
    name: "minimal-test-provider",
    model: "minimal-test-model",
    max_tool_calls: 16,
    capabilities: { ...DEFAULT_PROVIDER_CAPABILITIES },
    async plan(input) {
      provider_tools = input.tool_definitions.map((tool) => tool.name);
      return createProviderOutput({
        provider: this.name,
        model: this.model,
        assistant_message: "unsupported",
        tool_calls: [
          {
            tool_call_id: randomUUID(),
            tool_name: "entity.destroy",
            args: { entity_id },
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
  const service = new AgentService({
    session_manager: core.session_manager,
    tool_registry: core.tool_registry,
    command_executor: core.command_executor,
    provider,
    audit_log: { write: async () => {} },
  });
  const provider_result = await service.run({
    protocol_version: "1.0",
    request_id: randomUUID(),
    session_id: core.session.session_id,
    message: "try an unavailable tool",
    expected_revision: 1,
  });

  assert.deepEqual(provider_tools, MINIMAL_WORLD_PROFILE.supported_tools);
  assert.equal(provider_result.ok, false);
  assert.equal(undo_result.error.code, "UNDO_NOT_AVAILABLE");
  for (const result of [...rejected, provider_result]) {
    assert.equal(result.ok, false);
    assert.equal(result.world_revision, 1);
    assert.equal(result.undo_token, null);
  }
  assert.equal(endpointCount(fake, "execute"), before_execute_count);
  assert.equal(endpointCount(fake, "undo"), before_undo_count);
  assert.equal(fake.getAdapter(core.session.session_id, core.session.world_id).mock_world.revision, 1);
  assert.deepEqual(core.session.entity_context, before_context);
});

test("RemoteWorldAdapter rejects unsafe and invalid custom capability profiles", async (t) => {
  const tool_registry = createDefaultToolRegistry();
  const profiles = [
    {
      ...structuredClone(MINIMAL_WORLD_PROFILE),
      supports_idempotency: false,
    },
    {
      ...structuredClone(MINIMAL_WORLD_PROFILE),
      supported_tools: ["world.get_summary", "unknown.tool"],
    },
    {
      ...structuredClone(MINIMAL_WORLD_PROFILE),
      supported_tools: [
        ...MINIMAL_WORLD_PROFILE.supported_tools,
        "history.undo",
      ],
    },
  ];

  for (const capabilities of profiles) {
    const fake = await startFakeMahoWorldServer({
      profile: "custom",
      capabilities,
    });
    t.after(() => fake.close());
    const adapter = new RemoteWorldAdapter({
      session_id: randomUUID(),
      world_id: randomUUID(),
      tool_registry,
      base_url: fake.base_url,
      timeout_ms: 1_000,
    });
    t.after(() => adapter.close());
    await assert.rejects(
      adapter.health(),
      (error) =>
        error instanceof WorldAdapterError &&
        error.reason === "capability_insufficient"
    );
    assert.equal(fake.adapters.size, 0);
  }
});

test("HTTP tool and undo endpoints reject minimal capabilities locally", async (t) => {
  const fake = await startFakeMahoWorldServer({ profile: "minimal" });
  t.after(() => fake.close());
  const app = await startLegacyTestServer({
    world_config: {
      adapter_id: "remote",
      base_url: fake.base_url,
      timeout_ms: 1_000,
      auth_token: "",
      allow_non_loopback: false,
      endpoint_origin: fake.base_url,
      loopback: true,
    },
  });
  t.after(() => app.close());
  const created = await postJson(app.base_url, "/v1/sessions", {});
  const execute_count = endpointCount(fake, "execute");
  const undo_count = endpointCount(fake, "undo");
  const context = structuredClone(
    app.session_manager.getSession(created.body.session_id).entity_context
  );
  const unsupported = await postJson(app.base_url, "/v1/tools/execute", {
    protocol_version: "1.0",
    request_id: randomUUID(),
    session_id: created.body.session_id,
    world_id: created.body.world_id,
    tool_calls: [
      executorToolCall(
        "entity.destroy",
        { entity_id: randomUUID() },
        0
      ),
    ],
  });
  const undone = await postJson(app.base_url, "/v1/history/undo", {
    protocol_version: "1.0",
    request_id: randomUUID(),
    session_id: created.body.session_id,
    world_id: created.body.world_id,
    expected_revision: 0,
  });

  assert.equal(unsupported.status, 400);
  assert.equal(unsupported.body.error.code, "INVALID_REQUEST");
  assert.equal(undone.status, 400);
  assert.equal(undone.body.ok, false);
  assert.equal(undone.body.error.code, "UNDO_NOT_AVAILABLE");
  assert.equal(undone.body.world_revision, 0);
  assert.equal(undone.body.undo_token, null);
  assert.equal(endpointCount(fake, "execute"), execute_count);
  assert.equal(endpointCount(fake, "undo"), undo_count);
  assert.deepEqual(
    app.session_manager.getSession(created.body.session_id).entity_context,
    context
  );
});
