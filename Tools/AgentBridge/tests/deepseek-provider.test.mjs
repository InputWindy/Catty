import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import test from "node:test";
import { createPlanMessages } from "../src/agent/normalized-messages.mjs";
import { resolveProviderConfig } from "../src/agent/provider-config.mjs";
import { ProviderRegistry } from "../src/agent/provider-registry.mjs";
import {
  DEEPSEEK_DEFAULTS,
  createDeepSeekProvider,
} from "../src/agent/providers/presets/deepseek.mjs";
import { ProviderError } from "../src/agent/providers/provider-errors.mjs";
import { createDefaultToolRegistry } from "../src/tools/definitions.mjs";
import {
  completion,
  startFakeOpenAIServer,
} from "./helpers/fake-openai-server.mjs";

function planInput() {
  return {
    request_id: randomUUID(),
    session_id: randomUUID(),
    user_message: "hello",
    normalized_messages: createPlanMessages({
      system_message: "system",
      user_message: "hello",
    }),
    world_snapshot: {
      world_id: randomUUID(),
      revision: 0,
      entities: [],
      history: [],
    },
    session_context: {},
    tool_definitions: createDefaultToolRegistry().listDefinitions(),
  };
}

test("DeepSeek config uses explicit selection and API key precedence", () => {
  const config = resolveProviderConfig({
    env: {
      MAHO_AI_PROVIDER: "deepseek",
      MAHO_AI_API_KEY: "generic-key",
      DEEPSEEK_API_KEY: "deepseek-key",
    },
  });
  assert.equal(config.provider_id, "deepseek");
  assert.equal(config.api_key, "generic-key");
  assert.equal(config.thinking, "disabled");
  assert.equal(
    resolveProviderConfig({
      env: {
        MAHO_AI_PROVIDER: "deepseek",
        DEEPSEEK_API_KEY: "deepseek-key",
      },
    }).api_key,
    "deepseek-key"
  );
  assert.equal(
    resolveProviderConfig({
      env: {
        MAHO_AI_PROVIDER: "deepseek",
        MAHO_AI_API_KEY: "   ",
        DEEPSEEK_API_KEY: "deepseek-key",
      },
    }).api_key,
    "deepseek-key"
  );
  assert.equal(
    resolveProviderConfig({
      env: { DEEPSEEK_API_KEY: "deepseek-key" },
    }).provider_id,
    "mock"
  );
});

test("DeepSeek preset supplies centralized defaults and allows overrides", () => {
  const defaults = createDeepSeekProvider({
    api_key: "test-key",
    model: "",
    base_url: "",
    thinking: "disabled",
  });
  assert.equal(defaults.name, "deepseek");
  assert.equal(defaults.model, DEEPSEEK_DEFAULTS.model);
  assert.equal(defaults.base_url, DEEPSEEK_DEFAULTS.base_url);
  assert.equal(defaults.capabilities.supports_thinking, false);
  const overridden = createDeepSeekProvider({
    api_key: "test-key",
    model: "override-model",
    base_url: "http://127.0.0.1:1234/v1",
    thinking: "disabled",
  });
  assert.equal(overridden.model, "override-model");
});

test("DeepSeek missing key fails before any network request", () => {
  assert.throws(
    () =>
      createDeepSeekProvider({
        api_key: "",
        thinking: "disabled",
      }),
    (error) =>
      error instanceof ProviderError && error.reason === "api_key_missing"
  );
});

test("ProviderRegistry creates DeepSeek preset and sends thinking disabled", async (t) => {
  const fake = await startFakeOpenAIServer(() => ({
    body: completion({ content: "deepseek ok" }),
  }));
  t.after(() => fake.close());
  const registry = new ProviderRegistry({
    config: resolveProviderConfig({
      env: {
        MAHO_AI_PROVIDER: "deepseek",
        MAHO_AI_BASE_URL: fake.base_url,
        MAHO_AI_MODEL: "deepseek-test-model",
        DEEPSEEK_API_KEY: "test-key",
      },
    }),
  });
  const client = await registry.initialize();
  const output = await client.plan(planInput());
  assert.equal(output.provider, "deepseek");
  assert.equal(
    fake.requests[0].body.thinking.type,
    "disabled"
  );
  await registry.close();
});
