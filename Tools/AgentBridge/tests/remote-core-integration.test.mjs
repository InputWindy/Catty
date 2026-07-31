import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import test from "node:test";
import { CommandExecutor } from "../src/execution/command-executor.mjs";
import { SessionManager } from "../src/sessions/session-manager.mjs";
import { createDefaultToolRegistry } from "../src/tools/definitions.mjs";
import { WorldAdapterFactory } from "../src/world/world-adapter-factory.mjs";
import { startFakeCattyWorldServer } from "./helpers/fake-catty-world-server.mjs";

async function createCore(fake) {
  const tool_registry = createDefaultToolRegistry();
  const world_adapter_factory = new WorldAdapterFactory({
    config: {
      adapter_id: "remote",
      base_url: fake.base_url,
      timeout_ms: 1_000,
      auth_token: "",
      allow_non_loopback: false,
      endpoint_origin: fake.base_url,
      loopback: true,
    },
    tool_registry,
  });
  const session_manager = new SessionManager({
    world_adapter_factory,
  });
  const command_executor = new CommandExecutor({
    session_manager,
    tool_registry,
  });
  const session = session_manager.createSession();
  const initial = await session.adapter.getSnapshot({
    request_id: randomUUID(),
    session_id: session.session_id,
    world_id: session.world_id,
  });
  session.last_world_revision = initial.revision;
  return { command_executor, session, session_manager };
}

function spawnRequest(session) {
  return {
    protocol_version: "1.0",
    request_id: randomUUID(),
    session_id: session.session_id,
    world_id: session.world_id,
    tool_calls: [
      {
        tool_call_id: randomUUID(),
        tool_name: "entity.spawn_primitive",
        expected_revision: 0,
        dry_run: false,
        args: { primitive_type: "cube" },
      },
    ],
  };
}

test("EntityContext stays unchanged when a remote execute response is uncorrelated", async (t) => {
  const fake = await startFakeCattyWorldServer({
    response_handler({ phase, default_response }) {
      if (phase === "execute") {
        default_response.body.tool_results[0].tool_call_id =
          randomUUID();
      }
      return default_response;
    },
  });
  t.after(() => fake.close());
  const core = await createCore(fake);
  t.after(() => core.session_manager.close());

  const result = await core.command_executor.executeBatch(
    spawnRequest(core.session)
  );
  assert.equal(result.ok, false);
  assert.equal(
    result.error.details.adapter_reason,
    "correlation_mismatch"
  );
  assert.deepEqual(core.session.entity_context, {
    last_created_entity_id: null,
    last_referenced_entity_id: null,
    last_query_entity_ids: [],
    recent_entity_ids: [],
  });
});

test("EntityContext stays unchanged if the authoritative post-execution snapshot is invalid", async (t) => {
  let snapshot_count = 0;
  const fake = await startFakeCattyWorldServer({
    response_handler({ phase, default_response }) {
      if (phase === "snapshot") {
        snapshot_count += 1;
        if (snapshot_count === 2) {
          delete default_response.body.entities;
        }
      }
      return default_response;
    },
  });
  t.after(() => fake.close());
  const core = await createCore(fake);
  t.after(() => core.session_manager.close());

  const result = await core.command_executor.executeBatch(
    spawnRequest(core.session)
  );
  assert.equal(result.ok, true);
  assert.equal(result.world_revision, 1);
  assert.deepEqual(core.session.entity_context, {
    last_created_entity_id: null,
    last_referenced_entity_id: null,
    last_query_entity_ids: [],
    recent_entity_ids: [],
  });

  const authoritative = await core.session.adapter.getSnapshot({
    request_id: randomUUID(),
    session_id: core.session.session_id,
    world_id: core.session.world_id,
  });
  assert.equal(authoritative.revision, 1);
  assert.equal(authoritative.entities.length, 1);
});
