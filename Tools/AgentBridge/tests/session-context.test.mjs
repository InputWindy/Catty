import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import test from "node:test";
import { AgentService } from "../src/agent/agent-service.mjs";
import { MockProvider } from "../src/agent/providers/mock-provider.mjs";
import { createTestCore, execute, toolCall } from "./helpers/core.mjs";

function createService(core) {
  return new AgentService({
    session_manager: core.session_manager,
    tool_registry: core.tool_registry,
    command_executor: core.command_executor,
    provider: new MockProvider(),
    audit_log: { write: async () => {} },
  });
}

function request(session, message) {
  return {
    protocol_version: "1.0",
    request_id: randomUUID(),
    session_id: session.session_id,
    message,
    expected_revision: session.adapter.mock_world.revision,
  };
}

test("successful creation and unique query update Session entity context", async () => {
  const core = createTestCore();
  const service = createService(core);
  const spawned = await service.run(request(core.session, "生成一个立方体"));
  const entity_id = spawned.tool_results[0].data.entity.entity_id;

  assert.equal(core.session.entity_context.last_created_entity_id, entity_id);
  assert.equal(core.session.entity_context.last_referenced_entity_id, entity_id);
  assert.deepEqual(core.session.entity_context.recent_entity_ids, [entity_id]);

  await service.run(request(core.session, "查询 cube_1"));
  assert.deepEqual(core.session.entity_context.last_query_entity_ids, [entity_id]);
  assert.equal(core.session.entity_context.last_referenced_entity_id, entity_id);
});

test("destroy removes deleted entities from every Session reference field", async () => {
  const core = createTestCore();
  const spawned = await execute(core, [
    toolCall("entity.spawn_primitive", { primitive_type: "cube" }, 0),
  ]);
  const entity_id = spawned.tool_results[0].data.entity.entity_id;

  await execute(core, [
    toolCall("entity.destroy", { entity_id }, core.world.revision),
  ]);

  assert.deepEqual(core.session.entity_context, {
    last_created_entity_id: null,
    last_referenced_entity_id: null,
    last_query_entity_ids: [],
    recent_entity_ids: [],
  });
});

test("entity context is isolated between Sessions", async () => {
  const core = createTestCore();
  const second_session = core.session_manager.createSession();
  const first_spawn = await execute(core, [
    toolCall("entity.spawn_primitive", { primitive_type: "cube" }, 0),
  ]);

  const second_result = await core.command_executor.executeBatch({
    protocol_version: "1.0",
    request_id: randomUUID(),
    session_id: second_session.session_id,
    world_id: second_session.world_id,
    tool_calls: [
      toolCall("entity.spawn_primitive", { primitive_type: "sphere" }, 0),
    ],
  });

  assert.equal(
    core.session.entity_context.last_created_entity_id,
    first_spawn.tool_results[0].data.entity.entity_id
  );
  assert.equal(
    second_session.entity_context.last_created_entity_id,
    second_result.tool_results[0].data.entity.entity_id
  );
  assert.notEqual(
    core.session.entity_context.last_created_entity_id,
    second_session.entity_context.last_created_entity_id
  );
});

test("undo reconciles references that no longer exist", async () => {
  const core = createTestCore();
  const spawned = await execute(core, [
    toolCall("entity.spawn_primitive", { primitive_type: "cube" }, 0),
  ]);

  await execute(core, [
    toolCall("history.undo", { undo_token: spawned.undo_token }, 1),
  ]);

  assert.equal(core.world.entities.size, 0);
  assert.equal(core.session.entity_context.last_created_entity_id, null);
  assert.equal(core.session.entity_context.last_referenced_entity_id, null);
  assert.deepEqual(core.session.entity_context.recent_entity_ids, []);
});

test("recent entity context is deduplicated and bounded", async () => {
  const core = createTestCore();
  const calls = Array.from({ length: 25 }, () =>
    toolCall("entity.spawn_primitive", { primitive_type: "cube" }, 0)
  );
  const result = await execute(core, calls);
  const last_entity_id = result.tool_results.at(-1).data.entity.entity_id;

  assert.equal(result.ok, true);
  assert.equal(core.session.entity_context.recent_entity_ids.length, 20);
  assert.equal(
    new Set(core.session.entity_context.recent_entity_ids).size,
    20
  );
  assert.equal(
    core.session.entity_context.recent_entity_ids[0],
    last_entity_id
  );
  assert.equal(
    core.session.entity_context.last_created_entity_id,
    last_entity_id
  );
});
