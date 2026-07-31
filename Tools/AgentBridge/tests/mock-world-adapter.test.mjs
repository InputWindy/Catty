import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import test from "node:test";
import { createDefaultToolRegistry } from "../src/tools/definitions.mjs";
import { MockWorldAdapter } from "../src/world/mock-world-adapter.mjs";

function createAdapter() {
  const session_id = randomUUID();
  const world_id = randomUUID();
  return {
    session_id,
    world_id,
    adapter: new MockWorldAdapter({
      session_id,
      world_id,
      tool_registry: createDefaultToolRegistry(),
    }),
  };
}

function toolCall(tool_name, args = {}) {
  return {
    tool_call_id: randomUUID(),
    tool_name,
    args,
  };
}

async function execute(core, tool_calls, overrides = {}) {
  return core.adapter.executeTransaction({
    request_id: randomUUID(),
    session_id: core.session_id,
    world_id: core.world_id,
    expected_revision: core.adapter.mock_world.revision,
    dry_run: false,
    atomic: true,
    tool_calls,
    ...overrides,
  });
}

async function snapshot(core) {
  return core.adapter.getSnapshot({
    request_id: randomUUID(),
    session_id: core.session_id,
    world_id: core.world_id,
  });
}

test("MockWorldAdapter executes CRUD and preserves read-only revisions", async () => {
  const core = createAdapter();
  const spawned = await execute(core, [
    toolCall("entity.spawn_primitive", {
      primitive_type: "cube",
      name: "AdapterCube",
    }),
  ]);
  const entity_id = spawned.tool_results[0].data.entity.entity_id;
  assert.equal(spawned.after_revision, 1);
  assert.match(spawned.undo_token, /^[0-9a-f-]{36}$/);

  const queried = await execute(core, [
    toolCall("world.query_entities", { name: "AdapterCube" }),
  ]);
  assert.equal(queried.after_revision, 1);
  assert.equal(queried.tool_results[0].data.entities[0].entity_id, entity_id);

  const fetched = await execute(core, [
    toolCall("entity.get", { entity_id }),
  ]);
  assert.equal(fetched.after_revision, 1);
  assert.equal(fetched.tool_results[0].data.entity.name, "AdapterCube");

  await execute(core, [
    toolCall("entity.set_transform", {
      entity_id,
      transform: { position: [1, 2, 3] },
    }),
  ]);
  await execute(core, [
    toolCall("entity.set_property", {
      entity_id,
      property_name: "visible",
      value: false,
    }),
  ]);
  const destroyed = await execute(core, [
    toolCall("entity.destroy", { entity_id }),
  ]);
  assert.equal(destroyed.after_revision, 4);
  assert.equal((await snapshot(core)).entities.length, 0);
});

test("MockWorldAdapter batches are atomic and dry_run never commits or creates undo", async () => {
  const core = createAdapter();
  const failed = await execute(core, [
    toolCall("entity.spawn_primitive", { primitive_type: "cube" }),
    toolCall("entity.destroy", { entity_id: randomUUID() }),
  ]);
  assert.equal(failed.ok, false);
  assert.equal(failed.error.code, "ENTITY_NOT_FOUND");
  assert.equal(failed.before_revision, 0);
  assert.equal(failed.after_revision, 0);
  assert.equal((await snapshot(core)).entities.length, 0);

  const dry_run = await execute(
    core,
    [toolCall("entity.spawn_primitive", { primitive_type: "sphere" })],
    { dry_run: true }
  );
  assert.equal(dry_run.ok, true);
  assert.equal(dry_run.after_revision, 0);
  assert.equal(dry_run.undo_token, null);
  assert.equal(dry_run.changes[0].operation, "spawn");
  assert.equal((await snapshot(core)).entities.length, 0);
});

test("MockWorldAdapter enforces world-layer idempotency and revision conflicts", async () => {
  const core = createAdapter();
  const request_id = randomUUID();
  const call = toolCall("entity.spawn_primitive", {
    primitive_type: "cylinder",
  });
  const first = await execute(core, [call], { request_id });
  const replay = await execute(core, [call], {
    request_id,
    expected_revision: 0,
  });
  assert.equal(first.replayed, false);
  assert.equal(replay.replayed, true);
  assert.equal(
    replay.tool_results[0].data.entity.entity_id,
    first.tool_results[0].data.entity.entity_id
  );
  assert.equal((await snapshot(core)).entities.length, 1);

  const conflict = await execute(
    core,
    [toolCall("world.get_summary")],
    { expected_revision: 99 }
  );
  assert.equal(conflict.ok, false);
  assert.equal(conflict.error.code, "REVISION_CONFLICT");
});

test("MockWorldAdapter undo is authoritative, advances revision, and consumes tokens", async () => {
  const core = createAdapter();
  const spawned = await execute(core, [
    toolCall("entity.spawn_primitive", { primitive_type: "plane" }),
  ]);
  const undo_request = {
    request_id: randomUUID(),
    session_id: core.session_id,
    world_id: core.world_id,
    expected_revision: 1,
    undo_token: spawned.undo_token,
  };
  const undone = await core.adapter.undo(undo_request);
  assert.equal(undone.ok, true);
  assert.equal(undone.before_revision, 1);
  assert.equal(undone.after_revision, 2);
  assert.equal((await snapshot(core)).entities.length, 0);

  const replay = await core.adapter.undo(undo_request);
  assert.equal(replay.replayed, true);
  assert.equal((await snapshot(core)).revision, 2);

  const reused = await core.adapter.undo({
    ...undo_request,
    request_id: randomUUID(),
    expected_revision: 2,
  });
  assert.equal(reused.ok, false);
  assert.equal(reused.error.code, "UNDO_NOT_AVAILABLE");
});

test("MockWorldAdapter instances isolate Session state", async () => {
  const first = createAdapter();
  const second = createAdapter();
  await execute(first, [
    toolCall("entity.spawn_primitive", { primitive_type: "cube" }),
  ]);
  assert.equal((await snapshot(first)).entities.length, 1);
  assert.equal((await snapshot(second)).entities.length, 0);
});
