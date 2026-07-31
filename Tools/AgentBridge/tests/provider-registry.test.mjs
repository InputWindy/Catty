import assert from "node:assert/strict";
import test from "node:test";
import { ProviderRegistry } from "../src/agent/provider-registry.mjs";
import { resolveProviderConfig } from "../src/agent/provider-config.mjs";
import { CursorProvider } from "../src/agent/providers/cursor-provider.mjs";
import { MockProvider } from "../src/agent/providers/mock-provider.mjs";
import { OpenAICompatibleProvider } from "../src/agent/providers/openai-compatible-provider.mjs";

test("ProviderRegistry creates and closes MockProvider", async () => {
  const registry = new ProviderRegistry({
    config: resolveProviderConfig({ env: {} }),
  });
  const provider = await registry.initialize();
  assert.equal(provider instanceof MockProvider, true);
  assert.equal(registry.getMetadata().provider, "mock");
  await registry.close();
  assert.throws(() => registry.getSelectedProvider(), /not been initialized/);
});

test("ProviderRegistry retains CursorProvider without loading the SDK in Mock mode", async () => {
  const fake_agent = {};
  const registry = new ProviderRegistry({
    config: {
      ...resolveProviderConfig({ env: {} }),
      provider_id: "cursor",
      model: "cursor-test-model",
      api_key: "not-used",
    },
    factories: {
      cursor: async (config) =>
        new CursorProvider({ agent: fake_agent, model: config.model }),
    },
  });
  const provider = await registry.initialize();
  assert.equal(provider instanceof CursorProvider, true);
  assert.equal(provider.getAgent(), fake_agent);
  assert.equal(registry.getMetadata().model, "cursor-test-model");
  await registry.close();
});

test("ProviderRegistry creates generic openai-compatible Provider", async () => {
  const registry = new ProviderRegistry({
    config: resolveProviderConfig({
      env: {
        MAHO_AI_PROVIDER: "openai-compatible",
        MAHO_AI_BASE_URL: "http://127.0.0.1:1234/v1",
        MAHO_AI_MODEL: "generic-test-model",
        MAHO_AI_API_KEY: "test-key",
      },
    }),
  });
  const provider = await registry.initialize();
  assert.equal(provider instanceof OpenAICompatibleProvider, true);
  assert.equal(provider.name, "openai-compatible");
  assert.equal(provider.model, "generic-test-model");
  await registry.close();
});
