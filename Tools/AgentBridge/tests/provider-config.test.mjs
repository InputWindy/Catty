import assert from "node:assert/strict";
import test from "node:test";
import {
  resolveProviderConfig,
  selectProviderId,
} from "../src/agent/provider-config.mjs";
import { ProviderError } from "../src/agent/providers/provider-errors.mjs";

test("provider selection priority preserves Mock, explicit, Cursor, and default rules", () => {
  assert.equal(
    selectProviderId({
      env: {
        CATTY_AGENT_MOCK: "1",
        CATTY_AI_PROVIDER: "deepseek",
        CURSOR_API_KEY: "cursor-key",
      },
    }),
    "mock"
  );
  assert.equal(
    selectProviderId({
      env: {
        CATTY_AI_PROVIDER: "deepseek",
        CURSOR_API_KEY: "cursor-key",
      },
    }),
    "deepseek"
  );
  assert.equal(
    selectProviderId({ env: { CURSOR_API_KEY: "cursor-key" } }),
    "cursor"
  );
  assert.equal(selectProviderId({ env: {} }), "mock");
});

test("unknown providers return an explicit internal ProviderError", () => {
  assert.throws(
    () => selectProviderId({ env: { CATTY_AI_PROVIDER: "missing" } }),
    (error) =>
      error instanceof ProviderError &&
      error.reason === "provider_not_found"
  );
});

test("generic openai-compatible configuration requires all generic values", () => {
  assert.throws(
    () =>
      resolveProviderConfig({
        env: { CATTY_AI_PROVIDER: "openai-compatible" },
      }),
    /CATTY_AI_MODEL/
  );
  const config = resolveProviderConfig({
    env: {
      CATTY_AI_PROVIDER: "openai-compatible",
      CATTY_AI_BASE_URL: "http://127.0.0.1:1234/v1",
      CATTY_AI_MODEL: "local-model",
      CATTY_AI_API_KEY: "test-key",
    },
  });
  assert.equal(config.provider_id, "openai-compatible");
  assert.equal(config.model, "local-model");
  assert.equal(config.base_url, "http://127.0.0.1:1234/v1");
});

test("v0.3 rejects attempts to enable thinking mode", () => {
  assert.throws(
    () =>
      resolveProviderConfig({
        env: { CATTY_AI_THINKING: "enabled" },
      }),
    /Thinking mode is not supported/
  );
});
