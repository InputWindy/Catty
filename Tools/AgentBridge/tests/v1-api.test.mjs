import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import test from "node:test";
import {
  postJson,
  startLegacyTestServer,
} from "./helpers/test-server.mjs";
import { toolCall } from "./helpers/core.mjs";

async function createSession(app) {
  const created = await postJson(app.base_url, "/v1/sessions", {});
  assert.equal(created.status, 201);
  assert.equal(created.body.ok, true);
  return created.body;
}

test("v1 health and session creation are available", async (t) => {
  const app = await startLegacyTestServer();
  t.after(() => app.close());

  const health_response = await fetch(`${app.base_url}/v1/health`);
  assert.equal(health_response.status, 200);
  assert.deepEqual(await health_response.json(), {
    ok: true,
    protocol_version: "1.0",
    mock: true,
    busy: false,
    status: "mock (no CURSOR_API_KEY)",
  });

  const session = await createSession(app);
  assert.match(session.session_id, /^[0-9a-f-]{36}$/);
  assert.match(session.world_id, /^[0-9a-f-]{36}$/);
  assert.equal(session.world_revision, 0);
});

test("v1 tools execute and snapshot expose committed state", async (t) => {
  const app = await startLegacyTestServer();
  t.after(() => app.close());
  const session = await createSession(app);

  const executed = await postJson(app.base_url, "/v1/tools/execute", {
    protocol_version: "1.0",
    request_id: randomUUID(),
    session_id: session.session_id,
    world_id: session.world_id,
    tool_calls: [
      toolCall(
        "entity.spawn_primitive",
        { primitive_type: "cylinder" },
        0
      ),
    ],
  });
  assert.equal(executed.status, 200);
  assert.equal(executed.body.world_revision, 1);

  const snapshot_response = await fetch(
    `${app.base_url}/v1/world/snapshot?session_id=${session.session_id}`
  );
  assert.equal(snapshot_response.status, 200);
  const snapshot = await snapshot_response.json();
  assert.equal(snapshot.snapshot.entities.length, 1);
  assert.equal(snapshot.snapshot.entities[0].primitive_type, "cylinder");
});

test("v1 agent run uses MockProvider without an API key", async (t) => {
  const app = await startLegacyTestServer();
  t.after(() => app.close());
  const session = await createSession(app);

  const result = await postJson(app.base_url, "/v1/agent/run", {
    session_id: session.session_id,
    request_id: randomUUID(),
    message: "生成一个红色立方体",
    expected_revision: 0,
  });
  assert.equal(result.status, 200);
  assert.equal(result.body.ok, true);
  assert.equal(result.body.world_revision, 1);
  assert.deepEqual(
    result.body.tool_results[0].data.entity.properties.color,
    [1, 0, 0, 1]
  );
});

test("v1 history undo restores the latest transaction", async (t) => {
  const app = await startLegacyTestServer();
  t.after(() => app.close());
  const session = await createSession(app);
  const executed = await postJson(app.base_url, "/v1/tools/execute", {
    request_id: randomUUID(),
    session_id: session.session_id,
    tool_calls: [
      toolCall("entity.spawn_primitive", { primitive_type: "plane" }, 0),
    ],
  });

  const undone = await postJson(app.base_url, "/v1/history/undo", {
    request_id: randomUUID(),
    session_id: session.session_id,
    expected_revision: 1,
    undo_token: executed.body.undo_token,
  });
  assert.equal(undone.status, 200);
  assert.equal(undone.body.world_revision, 2);

  const snapshot_response = await fetch(
    `${app.base_url}/v1/world/snapshot?session_id=${session.session_id}`
  );
  const snapshot = await snapshot_response.json();
  assert.equal(snapshot.snapshot.entities.length, 0);
});

test("v1 errors use structured error JSON and status codes", async (t) => {
  const app = await startLegacyTestServer({ body_limit_bytes: 32 });
  t.after(() => app.close());

  const response = await fetch(`${app.base_url}/v1/sessions`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ padding: "x".repeat(100) }),
  });
  assert.equal(response.status, 413);
  const body = await response.json();
  assert.deepEqual(Object.keys(body.error), [
    "code",
    "message",
    "details",
    "retryable",
  ]);
  assert.equal(body.error.code, "REQUEST_TOO_LARGE");
});

