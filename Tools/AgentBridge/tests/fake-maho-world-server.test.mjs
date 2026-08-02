import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import test from "node:test";
import {
  MINIMAL_WORLD_PROFILE,
  startFakeMahoWorldServer,
} from "./helpers/fake-maho-world-server.mjs";

async function get(fake, pathname) {
  const response = await fetch(`${fake.base_url}${pathname}`);
  return {
    status: response.status,
    body: await response.json(),
  };
}

async function post(fake, pathname, body) {
  const response = await fetch(`${fake.base_url}${pathname}`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  });
  return {
    status: response.status,
    body: await response.json(),
  };
}

function identity() {
  return {
    adapter_protocol_version: "1.0",
    request_id: randomUUID(),
    session_id: randomUUID(),
    world_id: randomUUID(),
  };
}

function executeBody(tool_name, args) {
  return {
    ...identity(),
    expected_revision: 0,
    dry_run: false,
    atomic: true,
    tool_calls: [
      {
        tool_call_id: randomUUID(),
        tool_name,
        args,
      },
    ],
  };
}

test("FakeMahoWorldServer rejects extra request fields before creating world state", async (t) => {
  const fake = await startFakeMahoWorldServer();
  t.after(() => fake.close());
  const response = await post(
    fake,
    "/world-adapter/v1/snapshot",
    {
      ...identity(),
      unexpected: true,
    }
  );
  assert.equal(response.body.ok, false);
  assert.equal(response.body.error.code, "INVALID_ARGUMENT");
  assert.equal(fake.adapters.size, 0);
});

test("FakeMahoWorldServer returns structured unknown-tool and invalid-argument results", async (t) => {
  const fake = await startFakeMahoWorldServer();
  t.after(() => fake.close());

  const unknown = await post(
    fake,
    "/world-adapter/v1/execute",
    executeBody("world.not_registered", {})
  );
  assert.equal(unknown.body.ok, false);
  assert.equal(unknown.body.error.code, "UNKNOWN_TOOL");
  assert.equal(unknown.body.after_revision, 0);

  const invalid = await post(
    fake,
    "/world-adapter/v1/execute",
    executeBody("entity.set_transform", {
      entity_id: randomUUID(),
      transform: { position: [1, 2] },
    })
  );
  assert.equal(invalid.body.ok, false);
  assert.equal(invalid.body.error.code, "INVALID_ARGUMENT");
  assert.equal(invalid.body.after_revision, 0);
});

test("FakeMahoWorldServer minimal profile enforces capabilities and idempotency", async (t) => {
  const fake = await startFakeMahoWorldServer({ profile: "minimal" });
  t.after(() => fake.close());
  const health = await get(fake, "/world-adapter/v1/health");
  assert.equal(health.status, 200);
  assert.deepEqual(health.body.capabilities, MINIMAL_WORLD_PROFILE);

  const ids = identity();
  const request_id = ids.request_id;
  const spawn = {
    ...ids,
    expected_revision: 0,
    dry_run: false,
    atomic: false,
    tool_calls: [
      {
        tool_call_id: randomUUID(),
        tool_name: "entity.spawn_primitive",
        args: { primitive_type: "cube" },
      },
    ],
  };
  const first = await post(fake, "/world-adapter/v1/execute", spawn);
  const replay = await post(fake, "/world-adapter/v1/execute", spawn);
  assert.equal(first.body.ok, true);
  assert.equal(first.body.undo_token, null);
  assert.equal(first.body.tool_results[0].undo_token, null);
  assert.equal(replay.body.replayed, true);
  assert.equal(replay.body.request_id, request_id);
  assert.equal(
    replay.body.tool_results[0].data.entity.entity_id,
    first.body.tool_results[0].data.entity.entity_id
  );

  const unsupported = await post(fake, "/world-adapter/v1/execute", {
    ...identity(),
    session_id: ids.session_id,
    world_id: ids.world_id,
    expected_revision: 1,
    dry_run: false,
    atomic: false,
    tool_calls: [
      {
        tool_call_id: randomUUID(),
        tool_name: "entity.set_property",
        args: {
          entity_id: first.body.tool_results[0].data.entity.entity_id,
          property_name: "visible",
          value: false,
        },
      },
    ],
  });
  assert.equal(unsupported.body.ok, false);
  assert.equal(unsupported.body.error.code, "INVALID_REQUEST");

  const dry_run = await post(fake, "/world-adapter/v1/execute", {
    ...identity(),
    session_id: ids.session_id,
    world_id: ids.world_id,
    expected_revision: 1,
    dry_run: true,
    atomic: false,
    tool_calls: [
      {
        tool_call_id: randomUUID(),
        tool_name: "world.get_summary",
        args: {},
      },
    ],
  });
  assert.equal(dry_run.body.error.code, "INVALID_REQUEST");

  const too_many = await post(fake, "/world-adapter/v1/execute", {
    ...identity(),
    session_id: ids.session_id,
    world_id: ids.world_id,
    expected_revision: 1,
    dry_run: false,
    atomic: true,
    tool_calls: [
      {
        tool_call_id: randomUUID(),
        tool_name: "world.get_summary",
        args: {},
      },
      {
        tool_call_id: randomUUID(),
        tool_name: "world.get_summary",
        args: {},
      },
    ],
  });
  assert.equal(too_many.body.error.code, "INVALID_REQUEST");

  const undo_result = await post(fake, "/world-adapter/v1/undo", {
    ...identity(),
    session_id: ids.session_id,
    world_id: ids.world_id,
    expected_revision: 1,
    undo_token: null,
  });
  assert.equal(undo_result.body.error.code, "UNDO_NOT_AVAILABLE");
  assert.equal(fake.getAdapter(ids.session_id, ids.world_id).mock_world.revision, 1);
});
