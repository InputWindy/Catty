import assert from "node:assert/strict";
import test from "node:test";
import { loadConfig } from "../src/config.mjs";

test("missing API key selects Mock mode and keeps loopback defaults", () => {
  const config = loadConfig({
    argv: ["node", "server.mjs"],
    env: {},
    cwd: process.cwd(),
    logger: { error: () => {} },
  });

  assert.equal(config.host, "127.0.0.1");
  assert.equal(config.port, 8765);
  assert.equal(config.force_mock, true);
  assert.match(config.data_dir, /Tools[\\/]AgentBridge[\\/]\.runtime$/);
});

test("configuration rejects non-loopback listen hosts", () => {
  assert.throws(
    () =>
      loadConfig({
        argv: ["node", "server.mjs"],
        env: { CATTY_AGENT_HOST: "0.0.0.0" },
        logger: { error: () => {} },
      }),
    /loopback-only/
  );
});

