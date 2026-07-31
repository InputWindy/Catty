import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import test from "node:test";
import { createDefaultToolRegistry } from "../src/tools/definitions.mjs";
import { MockWorldAdapter } from "../src/world/mock-world-adapter.mjs";
import { RemoteWorldAdapter } from "../src/world/remote-world-adapter.mjs";
import {
  WorldAdapterFactory,
  resolveWorldAdapterConfig,
} from "../src/world/world-adapter-factory.mjs";
import { WorldAdapterError } from "../src/world/world-adapter-errors.mjs";

test("WorldAdapterFactory defaults to an isolated MockWorldAdapter", async () => {
  const tool_registry = createDefaultToolRegistry();
  const config = resolveWorldAdapterConfig({ env: {} });
  const factory = new WorldAdapterFactory({ config, tool_registry });
  const first = factory.createForSession();
  const second = factory.createForSession();

  assert.equal(config.adapter_id, "mock");
  assert.equal(first instanceof MockWorldAdapter, true);
  assert.equal(second instanceof MockWorldAdapter, true);
  assert.notEqual(first.world_id, second.world_id);
  await factory.close();
});

test("WorldAdapterFactory creates remote only after explicit selection", async () => {
  const config = resolveWorldAdapterConfig({
    env: {
      CATTY_WORLD_ADAPTER: "remote",
      CATTY_WORLD_BASE_URL: "http://localhost:8770",
      CATTY_WORLD_TIMEOUT_MS: "1234",
    },
  });
  const factory = new WorldAdapterFactory({
    config,
    tool_registry: createDefaultToolRegistry(),
  });
  const adapter = factory.createForSession({
    session_id: randomUUID(),
    world_id: randomUUID(),
  });

  assert.equal(adapter instanceof RemoteWorldAdapter, true);
  assert.equal(adapter.getMetadata().timeout_ms, 1234);
  assert.equal(factory.getMetadata().adapter, "remote");
  await factory.close();
});

test("unknown adapters and invalid remote configuration never fall back to mock", () => {
  assert.throws(
    () =>
      resolveWorldAdapterConfig({
        env: { CATTY_WORLD_ADAPTER: "missing" },
      }),
    (error) =>
      error instanceof WorldAdapterError &&
      error.reason === "configuration_error"
  );
  assert.throws(
    () =>
      resolveWorldAdapterConfig({
        env: {
          CATTY_WORLD_ADAPTER: "remote",
          CATTY_WORLD_BASE_URL: "not a URL",
        },
      }),
    (error) =>
      error instanceof WorldAdapterError &&
      error.reason === "invalid_base_url"
  );
});

test("remote URL policy allows loopback and guards non-loopback with opt-in plus token", () => {
  for (const base_url of [
    "http://127.0.0.1:8770",
    "http://localhost:8770",
    "http://[::1]:8770",
  ]) {
    assert.equal(
      resolveWorldAdapterConfig({
        env: {
          CATTY_WORLD_ADAPTER: "remote",
          CATTY_WORLD_BASE_URL: base_url,
        },
      }).loopback,
      true
    );
  }

  assert.throws(
    () =>
      resolveWorldAdapterConfig({
        env: {
          CATTY_WORLD_ADAPTER: "remote",
          CATTY_WORLD_BASE_URL: "https://world.example",
        },
      }),
    (error) => error.reason === "non_loopback_rejected"
  );
  assert.throws(
    () =>
      resolveWorldAdapterConfig({
        env: {
          CATTY_WORLD_ADAPTER: "remote",
          CATTY_WORLD_BASE_URL: "https://world.example",
          CATTY_WORLD_ALLOW_NON_LOOPBACK: "1",
        },
      }),
    (error) => error.reason === "auth_required"
  );
  const secret = "world-secret-must-not-leak";
  const allowed = resolveWorldAdapterConfig({
    env: {
      CATTY_WORLD_ADAPTER: "remote",
      CATTY_WORLD_BASE_URL: "https://world.example",
      CATTY_WORLD_ALLOW_NON_LOOPBACK: "1",
      CATTY_WORLD_AUTH_TOKEN: secret,
    },
  });
  const factory = new WorldAdapterFactory({
    config: allowed,
    tool_registry: createDefaultToolRegistry(),
  });
  assert.equal(JSON.stringify(factory.getMetadata()).includes(secret), false);
});
