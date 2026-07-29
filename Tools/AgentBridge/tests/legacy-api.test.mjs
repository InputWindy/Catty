import assert from "node:assert/strict";
import test from "node:test";
import { startLegacyTestServer } from "./helpers/test-server.mjs";

test("legacy health, chat, events, and shutdown remain compatible", async (t) => {
  const app = await startLegacyTestServer();
  t.after(() => app.close());

  const health_response = await fetch(`${app.base_url}/health`);
  assert.equal(health_response.status, 200);
  assert.deepEqual(await health_response.json(), {
    ok: true,
    mock: true,
    busy: false,
    status: "mock (no CURSOR_API_KEY)",
    cwd: process.cwd(),
  });

  const chat_response = await fetch(`${app.base_url}/chat`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ message: "baseline hello" }),
  });
  assert.equal(chat_response.status, 200);
  assert.deepEqual(await chat_response.json(), { accepted: true });

  await new Promise((resolve) => setTimeout(resolve, 450));
  const events_response = await fetch(`${app.base_url}/events?after=-1`);
  assert.equal(events_response.status, 200);
  const events_body = await events_response.json();
  assert.equal(events_body.busy, false);
  assert.equal(events_body.mock, true);
  assert.equal(events_body.status, "mock (no CURSOR_API_KEY)");
  assert.deepEqual(
    events_body.events.map(({ id, role }) => ({ id, role })),
    [
      { id: 0, role: "system" },
      { id: 1, role: "assistant" },
    ]
  );
  assert.match(events_body.events[1].text, /baseline hello/);

  const shutdown_response = await fetch(`${app.base_url}/shutdown`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: "{}",
  });
  assert.equal(shutdown_response.status, 200);
  assert.deepEqual(await shutdown_response.json(), { ok: true });
  assert.equal(app.wasShutdownRequested(), true);
});

test("legacy chat validation and not-found responses remain compatible", async (t) => {
  const app = await startLegacyTestServer();
  t.after(() => app.close());

  const invalid_chat_response = await fetch(`${app.base_url}/chat`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ message: "   " }),
  });
  assert.equal(invalid_chat_response.status, 400);
  assert.deepEqual(await invalid_chat_response.json(), {
    error: "message required",
  });

  const missing_response = await fetch(`${app.base_url}/missing`);
  assert.equal(missing_response.status, 404);
  assert.deepEqual(await missing_response.json(), { error: "not found" });
});

