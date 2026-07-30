import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import test from "node:test";
import {
  createTestCore,
  execute,
  toolCall,
} from "./helpers/core.mjs";

test("spawn, query, and get execute through the registry", async () => {
  const core = createTestCore();
  const spawned = await execute(core, [
    toolCall(
      "entity.spawn_primitive",
      { primitive_type: "cube", name: "CoreCube" },
      0
    ),
  ]);

  assert.equal(spawned.ok, true);
  assert.equal(spawned.world_revision, 1);
  assert.match(spawned.undo_token, /^[0-9a-f-]{36}$/);
  const entity_id = spawned.tool_results[0].data.entity.entity_id;

  const queried = await execute(core, [
    toolCall("world.query_entities", { name: "CoreCube" }, 1),
  ]);
  assert.equal(queried.ok, true);
  assert.equal(queried.world_revision, 1);
  assert.equal(queried.tool_results[0].data.entities[0].entity_id, entity_id);

  const fetched = await execute(core, [
    toolCall("entity.get", { entity_id }, 1),
  ]);
  assert.equal(fetched.tool_results[0].data.entity.name, "CoreCube");
  assert.equal(fetched.world_revision, 1);
});

test("set transform, set property, and destroy update the world", async () => {
  const core = createTestCore();
  const spawned = await execute(core, [
    toolCall("entity.spawn_primitive", { primitive_type: "sphere" }, 0),
  ]);
  const entity_id = spawned.tool_results[0].data.entity.entity_id;

  const transformed = await execute(core, [
    toolCall(
      "entity.set_transform",
      { entity_id, transform: { position: [10, 20, 30] } },
      1
    ),
  ]);
  assert.equal(transformed.ok, true);
  assert.deepEqual(
    core.world.getEntity(entity_id).transform.position,
    [10, 20, 30]
  );

  const property_result = await execute(core, [
    toolCall(
      "entity.set_property",
      { entity_id, property_name: "label", value: "target" },
      2
    ),
  ]);
  assert.equal(property_result.ok, true);
  assert.equal(core.world.getEntity(entity_id).properties.label, "target");

  const destroyed = await execute(core, [
    toolCall("entity.destroy", { entity_id }, 3),
  ]);
  assert.equal(destroyed.ok, true);
  assert.equal(core.world.entities.size, 0);
  assert.equal(core.world.revision, 4);
});

test("successful batch commits atomically with one revision and undo token", async () => {
  const core = createTestCore();
  const result = await execute(core, [
    toolCall("entity.spawn_primitive", { primitive_type: "cube" }, 0),
    toolCall("entity.spawn_primitive", { primitive_type: "plane" }, 0),
  ]);

  assert.equal(result.ok, true);
  assert.equal(core.world.entities.size, 2);
  assert.equal(core.world.revision, 1);
  assert.equal(result.tool_results[0].undo_token, result.undo_token);
  assert.equal(result.tool_results[1].undo_token, result.undo_token);
});

test("batch failure restores the complete pre-transaction Map state", async () => {
  const core = createTestCore();
  const result = await execute(core, [
    toolCall("entity.spawn_primitive", { primitive_type: "cube" }, 0),
    toolCall(
      "entity.destroy",
      { entity_id: randomUUID() },
      0
    ),
  ]);

  assert.equal(result.ok, false);
  assert.equal(result.error.code, "ENTITY_NOT_FOUND");
  assert.equal(result.failed_tool_call_index, 1);
  assert.equal(core.world.entities instanceof Map, true);
  assert.equal(core.world.entities.size, 0);
  assert.equal(core.world.revision, 0);
});

test("revision conflict prevents execution", async () => {
  const core = createTestCore();
  const result = await execute(core, [
    toolCall("entity.spawn_primitive", { primitive_type: "cube" }, 99),
  ]);

  assert.equal(result.ok, false);
  assert.equal(result.error.code, "REVISION_CONFLICT");
  assert.equal(core.world.entities.size, 0);
});

test("request_id idempotency replays the first result without mutation", async () => {
  const core = createTestCore();
  const request_id = randomUUID();
  const call = toolCall(
    "entity.spawn_primitive",
    { primitive_type: "cylinder" },
    0
  );
  const first = await execute(core, [call], { request_id });
  const second = await execute(core, [call], { request_id });

  assert.equal(first.replayed, false);
  assert.equal(second.replayed, true);
  assert.equal(second.tool_results[0].data.entity.entity_id,
    first.tool_results[0].data.entity.entity_id);
  assert.equal(core.world.entities.size, 1);
  assert.equal(core.world.revision, 1);
});

test("unknown tools, invalid arguments, and missing entities are stable errors", async () => {
  const unknown_core = createTestCore();
  const unknown = await execute(unknown_core, [
    toolCall("entity.teleport", {}, 0),
  ]);
  assert.equal(unknown.error.code, "UNKNOWN_TOOL");

  const invalid_core = createTestCore();
  const invalid = await execute(invalid_core, [
    toolCall(
      "entity.spawn_primitive",
      { primitive_type: "cube", arbitrary: true },
      0
    ),
  ]);
  assert.equal(invalid.error.code, "INVALID_ARGUMENT");

  const missing_core = createTestCore();
  const missing = await execute(missing_core, [
    toolCall("entity.get", { entity_id: randomUUID() }, 0),
  ]);
  assert.equal(missing.error.code, "ENTITY_NOT_FOUND");
});

test("dry run returns predicted changes without world or journal mutation", async () => {
  const core = createTestCore();
  const result = await execute(core, [
    toolCall(
      "entity.spawn_primitive",
      { primitive_type: "cube" },
      0,
      { dry_run: true }
    ),
  ]);

  assert.equal(result.ok, true);
  assert.equal(result.world_revision, 0);
  assert.equal(result.undo_token, null);
  assert.equal(result.tool_results[0].changes[0].operation, "spawn");
  assert.equal(core.world.entities.size, 0);
  assert.equal(core.undo_journal.records.size, 0);
});

