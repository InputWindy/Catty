import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import test from "node:test";
import { createDefaultToolRegistry } from "../src/tools/definitions.mjs";
import { RemoteWorldAdapter } from "../src/world/remote-world-adapter.mjs";
import { WorldAdapterError } from "../src/world/world-adapter-errors.mjs";
import { startFakeCattyWorldServer } from "./helpers/fake-catty-world-server.mjs";

function toolCall(tool_name, args = {}) {
  return {
    tool_call_id: randomUUID(),
    tool_name,
    args,
  };
}

function createRemote(fake, overrides = {}) {
  const session_id = randomUUID();
  const world_id = randomUUID();
  return {
    session_id,
    world_id,
    adapter: new RemoteWorldAdapter({
      session_id,
      world_id,
      tool_registry: createDefaultToolRegistry(),
      base_url: fake.base_url,
      timeout_ms: 1_000,
      health_cache_ttl_ms: 60_000,
      ...overrides,
    }),
  };
}

function snapshotInput(core, overrides = {}) {
  return {
    request_id: randomUUID(),
    session_id: core.session_id,
    world_id: core.world_id,
    ...overrides,
  };
}

function transactionInput(core, tool_calls, overrides = {}) {
  return {
    request_id: randomUUID(),
    session_id: core.session_id,
    world_id: core.world_id,
    expected_revision: 0,
    dry_run: false,
    atomic: true,
    tool_calls,
    ...overrides,
  };
}

test("RemoteWorldAdapter validates health and performs snapshot, execute, and undo", async (t) => {
  const fake = await startFakeCattyWorldServer();
  t.after(() => fake.close());
  const core = createRemote(fake);
  t.after(() => core.adapter.close());

  const health = await core.adapter.health();
  assert.equal(health.ok, true);
  assert.equal(health.adapter_protocol_version, "1.0");
  assert.equal(core.adapter.getMetadata().ready, true);

  const initial = await core.adapter.getSnapshot(snapshotInput(core));
  assert.equal(initial.revision, 0);
  assert.deepEqual(initial.entities, []);

  const spawned = await core.adapter.executeTransaction(
    transactionInput(core, [
      toolCall("entity.spawn_primitive", {
        primitive_type: "cube",
      }),
    ])
  );
  assert.equal(spawned.ok, true);
  assert.equal(spawned.before_revision, 0);
  assert.equal(spawned.after_revision, 1);
  assert.match(spawned.undo_token, /^[0-9a-f-]{36}$/);

  const undone = await core.adapter.undo({
    request_id: randomUUID(),
    session_id: core.session_id,
    world_id: core.world_id,
    expected_revision: 1,
    undo_token: spawned.undo_token,
  });
  assert.equal(undone.ok, true);
  assert.equal(undone.after_revision, 2);
  const final = await core.adapter.getSnapshot(snapshotInput(core));
  assert.equal(final.revision, 2);
  assert.deepEqual(final.entities, []);
});

test("RemoteWorldAdapter preserves atomic rollback, dry_run, and world idempotency", async (t) => {
  const fake = await startFakeCattyWorldServer();
  t.after(() => fake.close());
  const core = createRemote(fake);
  t.after(() => core.adapter.close());

  const failed = await core.adapter.executeTransaction(
    transactionInput(core, [
      toolCall("entity.spawn_primitive", { primitive_type: "cube" }),
      toolCall("entity.destroy", { entity_id: randomUUID() }),
    ])
  );
  assert.equal(failed.ok, false);
  assert.equal(failed.error.code, "ENTITY_NOT_FOUND");
  assert.equal(failed.after_revision, 0);

  const dry_run = await core.adapter.executeTransaction(
    transactionInput(
      core,
      [toolCall("entity.spawn_primitive", { primitive_type: "sphere" })],
      { dry_run: true }
    )
  );
  assert.equal(dry_run.ok, true);
  assert.equal(dry_run.after_revision, 0);
  assert.equal(dry_run.undo_token, null);

  const request_id = randomUUID();
  const call = toolCall("entity.spawn_primitive", {
    primitive_type: "plane",
  });
  const first = await core.adapter.executeTransaction(
    transactionInput(core, [call], { request_id })
  );
  const replay = await core.adapter.executeTransaction(
    transactionInput(core, [call], {
      request_id,
      expected_revision: 0,
    })
  );
  assert.equal(first.replayed, false);
  assert.equal(replay.replayed, true);
  assert.equal(
    replay.tool_results[0].data.entity.entity_id,
    first.tool_results[0].data.entity.entity_id
  );
  const snapshot = await core.adapter.getSnapshot(snapshotInput(core));
  assert.equal(snapshot.entities.length, 1);
});

test("RemoteWorldAdapter rejects incompatible protocol and insufficient capabilities", async (t) => {
  const incompatible = await startFakeCattyWorldServer({
    response_handler({ phase, default_response }) {
      if (phase === "health") {
        default_response.body.adapter_protocol_version = "2.0";
      }
      return default_response;
    },
  });
  t.after(() => incompatible.close());
  const first = createRemote(incompatible);
  await assert.rejects(
    first.adapter.health(),
    (error) =>
      error instanceof WorldAdapterError &&
      error.reason === "protocol_version_incompatible"
  );
  await first.adapter.close();

  const insufficient = await startFakeCattyWorldServer({
    capabilities: { supports_undo: false },
  });
  t.after(() => insufficient.close());
  const second = createRemote(insufficient);
  await assert.rejects(
    second.adapter.health(),
    (error) =>
      error instanceof WorldAdapterError &&
      error.reason === "capability_insufficient"
  );
  await second.adapter.close();
});

test("RemoteWorldAdapter rejects response correlation mismatches and missing fields", async (t) => {
  for (const field of ["request_id", "session_id", "world_id"]) {
    const fake = await startFakeCattyWorldServer({
      response_handler({ phase, default_response }) {
        if (phase === "snapshot") {
          default_response.body[field] = randomUUID();
        }
        return default_response;
      },
    });
    const core = createRemote(fake);
    await assert.rejects(
      core.adapter.getSnapshot(snapshotInput(core)),
      (error) =>
        error instanceof WorldAdapterError &&
        error.reason === "correlation_mismatch",
      field
    );
    await core.adapter.close();
    await fake.close();
  }

  const missing = await startFakeCattyWorldServer({
    response_handler({ phase, default_response }) {
      if (phase === "snapshot") {
        delete default_response.body.entities;
      }
      return default_response;
    },
  });
  t.after(() => missing.close());
  const core = createRemote(missing);
  await assert.rejects(
    core.adapter.getSnapshot(snapshotInput(core)),
    (error) =>
      error instanceof WorldAdapterError &&
      error.reason === "response_invalid"
  );
  await core.adapter.close();
});

test("RemoteWorldAdapter maps auth, HTTP, non-JSON, and connection failures without leaking tokens", async (t) => {
  const secret = "remote-world-secret";
  const authenticated = await startFakeCattyWorldServer({
    auth_token: secret,
  });
  t.after(() => authenticated.close());
  const rejected = createRemote(authenticated);
  let auth_error;
  try {
    await rejected.adapter.health();
  } catch (error) {
    auth_error = error;
  }
  assert.equal(auth_error.http_status, 401);
  assert.equal(JSON.stringify(auth_error).includes(secret), false);
  await rejected.adapter.close();

  const accepted = createRemote(authenticated, {
    auth_token: secret,
  });
  assert.equal((await accepted.adapter.health()).ok, true);
  assert.equal(
    JSON.stringify(accepted.adapter.getMetadata()).includes(secret),
    false
  );
  await accepted.adapter.close();

  const invalid_json = await startFakeCattyWorldServer({
    response_handler({ phase, default_response }) {
      return phase === "health"
        ? { ...default_response, raw: "not-json" }
        : default_response;
    },
  });
  t.after(() => invalid_json.close());
  const invalid = createRemote(invalid_json);
  await assert.rejects(
    invalid.adapter.health(),
    (error) => error.reason === "response_invalid_json"
  );
  await invalid.adapter.close();

  const temporary = await startFakeCattyWorldServer();
  const unavailable_url = temporary.base_url;
  await temporary.close();
  const unavailable = createRemote({ base_url: unavailable_url });
  await assert.rejects(
    unavailable.adapter.health(),
    (error) => error.reason === "connection_failed"
  );
  await unavailable.adapter.close();
});

test("RemoteWorldAdapter maps HTTP 409 and 500 without retrying", async () => {
  for (const expected of [
    {
      status: 409,
      reason: "revision_conflict",
      retryable: true,
    },
    {
      status: 500,
      reason: "http_error",
      retryable: true,
    },
  ]) {
    const fake = await startFakeCattyWorldServer({
      response_handler({ phase, default_response }) {
        return phase === "health"
          ? {
              ...default_response,
              status: expected.status,
              body: { ok: false },
            }
          : default_response;
      },
    });
    const core = createRemote(fake);
    await assert.rejects(
      core.adapter.health(),
      (error) =>
        error.reason === expected.reason &&
        error.http_status === expected.status &&
        error.retryable === expected.retryable
    );
    assert.equal(fake.requests.length, 1);
    await core.adapter.close();
    await fake.close();
  }
});

test("RemoteWorldAdapter rejects malformed execute correlation and result cardinality", async () => {
  for (const mutate of [
    (body) => {
      body.request_id = randomUUID();
    },
    (body) => {
      body.tool_results[0].tool_call_id = randomUUID();
    },
    (body) => {
      body.tool_results = [];
    },
  ]) {
    const fake = await startFakeCattyWorldServer({
      response_handler({ phase, default_response }) {
        if (phase === "execute") {
          mutate(default_response.body);
        }
        return default_response;
      },
    });
    const core = createRemote(fake);
    await assert.rejects(
      core.adapter.executeTransaction(
        transactionInput(core, [
          toolCall("entity.spawn_primitive", {
            primitive_type: "cube",
          }),
        ])
      ),
      WorldAdapterError
    );
    await core.adapter.close();
    await fake.close();
  }
});

test("RemoteWorldAdapter distinguishes timeout, external cancellation, and shutdown cancellation", async (t) => {
  const delayed = await startFakeCattyWorldServer({
    response_handler({ phase, default_response }) {
      return phase === "health"
        ? { ...default_response, delay_ms: 200 }
        : default_response;
    },
  });
  t.after(() => delayed.close());

  const timeout = createRemote(delayed, { timeout_ms: 10 });
  await assert.rejects(
    timeout.adapter.health(),
    (error) => error.reason === "request_timeout" && error.timeout
  );
  await timeout.adapter.close();

  const cancelled = createRemote(delayed);
  const controller = new AbortController();
  const request = cancelled.adapter.health({
    signal: controller.signal,
  });
  controller.abort();
  await assert.rejects(
    request,
    (error) => error.reason === "request_cancelled" && error.cancelled
  );
  await cancelled.adapter.close();

  const closing = createRemote(delayed);
  const in_flight = closing.adapter.health();
  await closing.adapter.close();
  await assert.rejects(
    in_flight,
    (error) => error.reason === "request_cancelled" && error.cancelled
  );
});

test("RemoteWorldAdapter reports an interrupted response as a connection failure", async () => {
  const fake = await startFakeCattyWorldServer({
    response_handler({ phase, default_response }) {
      return phase === "snapshot"
        ? { ...default_response, destroy: true }
        : default_response;
    },
  });
  const core = createRemote(fake);
  await assert.rejects(
    core.adapter.getSnapshot(snapshotInput(core)),
    (error) =>
      error instanceof WorldAdapterError &&
      error.reason === "connection_failed" &&
      error.retryable
  );
  await core.adapter.close();
  await fake.close();
});

test("RemoteWorldAdapter rejects non-finite numbers and prototype pollution in responses", async () => {
  for (const raw of [
    '{"ok":true,"adapter_protocol_version":"1.0","server_name":"fake","server_version":"1","capabilities":{"supports_atomic_transactions":true,"supports_dry_run":true,"supports_undo":true,"supports_idempotency":true,"max_tool_calls":1e400,"supported_tools":[]}}',
    '{"ok":true,"adapter_protocol_version":"1.0","server_name":"fake","server_version":"1","capabilities":{"supports_atomic_transactions":true,"supports_dry_run":true,"supports_undo":true,"supports_idempotency":true,"max_tool_calls":16,"supported_tools":[],"__proto__":{"polluted":true}}}',
  ]) {
    const fake = await startFakeCattyWorldServer({
      response_handler({ phase, default_response }) {
        return phase === "health"
          ? { ...default_response, raw }
          : default_response;
      },
    });
    const core = createRemote(fake);
    await assert.rejects(core.adapter.health(), WorldAdapterError);
    await core.adapter.close();
    await fake.close();
  }
});
