import assert from "node:assert/strict";
import test from "node:test";
import {
  createTestCore,
  execute,
  toolCall,
} from "./helpers/core.mjs";

async function undo(core, undo_token, expected_revision) {
  return execute(core, [
    toolCall("history.undo", { undo_token }, expected_revision),
  ]);
}

test("undo spawn restores the empty world and advances revision", async () => {
  const core = createTestCore();
  const spawned = await execute(core, [
    toolCall("entity.spawn_primitive", { primitive_type: "cube" }, 0),
  ]);
  const undone = await undo(core, spawned.undo_token, 1);

  assert.equal(undone.ok, true);
  assert.equal(core.world.entities.size, 0);
  assert.equal(core.world.revision, 2);
  assert.equal(undone.world_revision, 2);

  const repeated = await undo(core, spawned.undo_token, 2);
  assert.equal(repeated.ok, false);
  assert.equal(repeated.error.code, "UNDO_NOT_AVAILABLE");
});

test("undo transform restores the previous transform", async () => {
  const core = createTestCore();
  const spawned = await execute(core, [
    toolCall("entity.spawn_primitive", { primitive_type: "cube" }, 0),
  ]);
  const entity_id = spawned.tool_results[0].data.entity.entity_id;
  const transformed = await execute(core, [
    toolCall(
      "entity.set_transform",
      { entity_id, transform: { scale: [2, 3, 4] } },
      1
    ),
  ]);

  const undone = await undo(core, transformed.undo_token, 2);
  assert.equal(undone.ok, true);
  assert.deepEqual(core.world.getEntity(entity_id).transform.scale, [1, 1, 1]);
  assert.equal(core.world.revision, 3);
});

test("undo destroy restores the complete entity", async () => {
  const core = createTestCore();
  const spawned = await execute(core, [
    toolCall(
      "entity.spawn_primitive",
      {
        primitive_type: "sphere",
        properties: { color: [0.25, 0.5, 0.75, 1] },
      },
      0
    ),
  ]);
  const entity = spawned.tool_results[0].data.entity;
  const destroyed = await execute(core, [
    toolCall("entity.destroy", { entity_id: entity.entity_id }, 1),
  ]);

  const undone = await undo(core, destroyed.undo_token, 2);
  assert.equal(undone.ok, true);
  assert.deepEqual(core.world.getEntity(entity.entity_id), entity);
  assert.equal(core.world.revision, 3);
});

test("one undo token restores an entire successful batch", async () => {
  const core = createTestCore();
  const batch = await execute(core, [
    toolCall("entity.spawn_primitive", { primitive_type: "cube" }, 0),
    toolCall("entity.spawn_primitive", { primitive_type: "plane" }, 0),
  ]);

  const undone = await undo(core, batch.undo_token, 1);
  assert.equal(undone.ok, true);
  assert.equal(core.world.entities.size, 0);
  assert.equal(core.world.revision, 2);
});

