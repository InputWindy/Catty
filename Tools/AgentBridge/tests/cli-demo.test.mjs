import assert from "node:assert/strict";
import test from "node:test";
import {
  createDemoState,
  getSessionSnapshot,
  handleDemoInput,
  runDemo,
} from "../src/cli/demo.mjs";
import { Readable, Writable } from "node:stream";
import { startFakeCattyWorldServer } from "./helpers/fake-catty-world-server.mjs";

test("CLI natural language, world, and entity commands use the active Core", async () => {
  const state = createDemoState();
  const created = await handleDemoInput(state, "生成一个红色立方体");
  assert.equal(created.exit, false);
  assert.match(created.output, /entity\.spawn_primitive ✓/);
  assert.match(created.output, /World revision: 1/);

  const world = await handleDemoInput(state, "/world");
  assert.match(world.output, /"revision": 1/);
  assert.match(world.output, /"color": \[/);

  const entities = await handleDemoInput(state, "/entities");
  assert.match(entities.output, /cube_1 \[cube\]/);
});

test("CLI undo delegates to AgentService and reset creates a fresh Session", async () => {
  const state = createDemoState();
  const first_session_id = state.active_session.session_id;
  await handleDemoInput(state, "生成一个立方体");

  const undone = await handleDemoInput(state, "/undo");
  assert.match(undone.output, /history\.undo ✓/);
  const undone_snapshot = await getSessionSnapshot(
    state.active_session
  );
  assert.equal(undone_snapshot.entities.length, 0);
  assert.equal(undone_snapshot.revision, 2);

  const reset = await handleDemoInput(state, "/reset");
  assert.match(reset.output, /Started new Session/);
  assert.notEqual(state.active_session.session_id, first_session_id);
  const reset_snapshot = await getSessionSnapshot(
    state.active_session
  );
  assert.equal(reset_snapshot.revision, 0);
  assert.equal(reset_snapshot.entities.length, 0);
});

test("CLI help, unknown commands, blank input, and exit are deterministic", async () => {
  const state = createDemoState();
  assert.match((await handleDemoInput(state, "/help")).output, /\/world/);
  assert.match(
    (await handleDemoInput(state, "/missing")).output,
    /Unknown command/
  );
  assert.deepEqual(await handleDemoInput(state, "   "), {
    exit: false,
    output: "",
  });
  assert.deepEqual(await handleDemoInput(state, "/exit"), {
    exit: true,
    output: "Bye.",
  });
});

test("CLI reports command errors without throwing", async () => {
  const state = createDemoState();
  state.agent_service.run = async () => {
    throw new Error("simulated failure");
  };
  const result = await handleDemoInput(state, "生成一个立方体");
  assert.equal(result.exit, false);
  assert.equal(result.output, "Error: simulated failure");
});

test("CLI startup defaults to MockProvider and never requires a real key", async () => {
  let text = "";
  const output = new Writable({
    write(chunk, _encoding, callback) {
      text += chunk.toString();
      callback();
    },
  });
  const exit_code = await runDemo({
    input: Readable.from([]),
    output,
    env: {},
  });
  assert.equal(exit_code, 0);
  assert.match(text, /Catty Agent Core v0\.4/);
  assert.match(text, /Provider: mock/);
  assert.match(text, /Mode: Mock/);
  assert.match(text, /Thinking: disabled/);
  assert.match(text, /World adapter: mock/);
  assert.match(text, /Adapter readiness: ready/);
});

test("CLI uses remote only when explicitly selected and closes cleanly", async (t) => {
  const secret = "cli-remote-test-secret";
  const fake = await startFakeCattyWorldServer({
    auth_token: secret,
  });
  t.after(() => fake.close());
  let text = "";
  const output = new Writable({
    write(chunk, _encoding, callback) {
      text += chunk.toString();
      callback();
    },
  });
  const exit_code = await runDemo({
    input: Readable.from([]),
    output,
    env: {
      CATTY_WORLD_ADAPTER: "remote",
      CATTY_WORLD_BASE_URL: fake.base_url,
      CATTY_WORLD_AUTH_TOKEN: secret,
      CATTY_WORLD_TIMEOUT_MS: "1000",
    },
  });

  assert.equal(exit_code, 0);
  assert.match(text, /World adapter: remote/);
  assert.match(text, /Adapter readiness: ready/);
  assert.match(text, new RegExp(fake.base_url.replaceAll(".", "\\.")));
  assert.equal(text.includes(secret), false);
  assert.ok(fake.requests.some((request) => request.pathname.endsWith("/health")));
  assert.ok(fake.requests.some((request) => request.pathname.endsWith("/snapshot")));
  assert.equal(
    fake.requests.every((request) => !("authorization" in request)),
    true
  );
});
