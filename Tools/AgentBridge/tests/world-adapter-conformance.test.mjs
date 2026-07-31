import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import test from "node:test";
import { createDefaultToolRegistry } from "../src/tools/definitions.mjs";
import { MockWorldAdapter } from "../src/world/mock-world-adapter.mjs";
import { RemoteWorldAdapter } from "../src/world/remote-world-adapter.mjs";
import { startFakeMahoWorldServer } from "./helpers/fake-maho-world-server.mjs";

function createIdentity() {
  return {
    session_id: randomUUID(),
    world_id: randomUUID(),
  };
}

function toolCall(tool_name, args = {}) {
  return {
    tool_call_id: randomUUID(),
    tool_name,
    args,
  };
}

async function createMockHarness() {
  const tool_registry = createDefaultToolRegistry();
  const first = createIdentity();
  const second = createIdentity();
  return {
    first: {
      ...first,
      adapter: new MockWorldAdapter({
        ...first,
        tool_registry,
        max_tool_calls: 16,
      }),
    },
    second: {
      ...second,
      adapter: new MockWorldAdapter({
        ...second,
        tool_registry,
        max_tool_calls: 16,
      }),
    },
    async close() {
      await Promise.all([
        this.first.adapter.close(),
        this.second.adapter.close(),
      ]);
    },
  };
}

async function createRemoteHarness() {
  const fake = await startFakeMahoWorldServer();
  const tool_registry = createDefaultToolRegistry();
  const first = createIdentity();
  const second = createIdentity();
  const options = {
    tool_registry,
    base_url: fake.base_url,
    timeout_ms: 1_000,
  };
  return {
    first: {
      ...first,
      adapter: new RemoteWorldAdapter({ ...first, ...options }),
    },
    second: {
      ...second,
      adapter: new RemoteWorldAdapter({ ...second, ...options }),
    },
    async close() {
      await Promise.all([
        this.first.adapter.close(),
        this.second.adapter.close(),
      ]);
      await fake.close();
    },
  };
}

function snapshot(core) {
  return core.adapter.getSnapshot({
    request_id: randomUUID(),
    session_id: core.session_id,
    world_id: core.world_id,
  });
}

function execute(core, expected_revision, tool_calls, overrides = {}) {
  return core.adapter.executeTransaction({
    request_id: randomUUID(),
    session_id: core.session_id,
    world_id: core.world_id,
    expected_revision,
    dry_run: false,
    atomic: true,
    tool_calls,
    ...overrides,
  });
}

async function assertAdapterConformance(createHarness) {
  const harness = await createHarness();
  try {
    const { first, second } = harness;
    const health = await first.adapter.health();
    assert.equal(health.ok, true);
    assert.equal(health.adapter_protocol_version, "1.0");
    assert.equal(health.capabilities.supports_atomic_transactions, true);
    assert.equal(health.capabilities.supports_dry_run, true);
    assert.equal(health.capabilities.supports_undo, true);
    assert.equal(health.capabilities.supports_idempotency, true);

    assert.deepEqual(await snapshot(first), {
      world_id: first.world_id,
      revision: 0,
      entities: [],
      history: [],
    });
    assert.equal((await snapshot(second)).revision, 0);

    const dry_run = await execute(
      first,
      0,
      [
        toolCall("entity.spawn_primitive", {
          primitive_type: "sphere",
        }),
      ],
      { dry_run: true }
    );
    assert.equal(dry_run.ok, true);
    assert.equal(dry_run.after_revision, 0);
    assert.equal(dry_run.undo_token, null);
    assert.equal(dry_run.changes[0].operation, "spawn");
    assert.equal((await snapshot(first)).entities.length, 0);

    const rolled_back = await execute(first, 0, [
      toolCall("entity.spawn_primitive", {
        primitive_type: "plane",
      }),
      toolCall("entity.destroy", { entity_id: randomUUID() }),
    ]);
    assert.equal(rolled_back.ok, false);
    assert.equal(rolled_back.error.code, "ENTITY_NOT_FOUND");
    assert.equal(rolled_back.before_revision, 0);
    assert.equal(rolled_back.after_revision, 0);
    assert.equal((await snapshot(first)).entities.length, 0);

    const spawned = await execute(first, 0, [
      toolCall("entity.spawn_primitive", {
        primitive_type: "cube",
        name: "ConformanceCube",
      }),
    ]);
    const entity_id = spawned.tool_results[0].data.entity.entity_id;
    assert.equal(spawned.ok, true);
    assert.equal(spawned.after_revision, 1);
    assert.match(spawned.undo_token, /^[0-9a-f-]{36}$/);

    const read = await execute(first, 1, [
      toolCall("world.query_entities", { name: "ConformanceCube" }),
      toolCall("entity.get", { entity_id }),
    ]);
    assert.equal(read.ok, true);
    assert.equal(read.after_revision, 1);
    assert.equal(read.tool_results[0].data.entities[0].entity_id, entity_id);
    assert.equal(read.tool_results[1].data.entity.entity_id, entity_id);

    const request_id = randomUUID();
    const transform_call = toolCall("entity.set_transform", {
      entity_id,
      transform: { position: [4, 2, -1] },
    });
    const property_call = toolCall("entity.set_property", {
      entity_id,
      property_name: "visible",
      value: false,
    });
    const updated = await execute(
      first,
      1,
      [transform_call, property_call],
      { request_id }
    );
    assert.equal(updated.ok, true);
    assert.equal(updated.after_revision, 2);
    assert.equal(updated.tool_results.length, 2);
    assert.equal(updated.tool_results[0].undo_token, updated.undo_token);
    assert.equal(updated.tool_results[1].undo_token, updated.undo_token);

    const replayed = await execute(
      first,
      1,
      [transform_call, property_call],
      { request_id }
    );
    assert.equal(replayed.replayed, true);
    assert.equal(replayed.after_revision, 2);
    assert.equal(replayed.undo_token, updated.undo_token);

    const conflict = await execute(first, 0, [
      toolCall("world.get_summary"),
    ]);
    assert.equal(conflict.ok, false);
    assert.equal(conflict.error.code, "REVISION_CONFLICT");
    assert.equal(conflict.after_revision, 2);

    const undone = await first.adapter.undo({
      request_id: randomUUID(),
      session_id: first.session_id,
      world_id: first.world_id,
      expected_revision: 2,
      undo_token: updated.undo_token,
    });
    assert.equal(undone.ok, true);
    assert.equal(undone.before_revision, 2);
    assert.equal(undone.after_revision, 3);
    const after_undo = await snapshot(first);
    assert.deepEqual(after_undo.entities[0].transform.position, [0, 0, 0]);
    assert.equal(after_undo.entities[0].properties.visible, true);

    const reused = await first.adapter.undo({
      request_id: randomUUID(),
      session_id: first.session_id,
      world_id: first.world_id,
      expected_revision: 3,
      undo_token: updated.undo_token,
    });
    assert.equal(reused.ok, false);
    assert.equal(reused.error.code, "UNDO_NOT_AVAILABLE");
    assert.equal(reused.after_revision, 3);

    const destroyed = await execute(first, 3, [
      toolCall("entity.destroy", { entity_id }),
    ]);
    assert.equal(destroyed.ok, true);
    assert.equal(destroyed.after_revision, 4);
    assert.deepEqual((await snapshot(first)).entities, []);

    const isolated = await snapshot(second);
    assert.equal(isolated.revision, 0);
    assert.deepEqual(isolated.entities, []);
  } finally {
    await harness.close();
  }
}

test("MockWorldAdapter satisfies the shared WorldAdapter conformance suite", () =>
  assertAdapterConformance(createMockHarness));

test("RemoteWorldAdapter satisfies the shared WorldAdapter conformance suite", () =>
  assertAdapterConformance(createRemoteHarness));
