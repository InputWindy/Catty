import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import test from "node:test";
import { startFakeMahoWorldServer } from "./helpers/fake-maho-world-server.mjs";

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
