import { assertProviderContract } from "./provider-contract.mjs";
import { MockProvider } from "./providers/mock-provider.mjs";
import { CursorProvider } from "./providers/cursor-provider.mjs";
import {
  ProviderError,
  asProviderError,
  providerErrorReasons,
} from "./providers/provider-errors.mjs";

function defaultFactories() {
  return new Map([
    [
      "mock",
      async (config) => new MockProvider({ model: config.model }),
    ],
    [
      "cursor",
      async (config) => CursorProvider.create(config),
    ],
    [
      "openai-compatible",
      async (config) => {
        const { OpenAICompatibleProvider } = await import(
          "./providers/openai-compatible-provider.mjs"
        );
        return new OpenAICompatibleProvider(config);
      },
    ],
    [
      "deepseek",
      async (config) => {
        const { createDeepSeekProvider } = await import(
          "./providers/presets/deepseek.mjs"
        );
        return createDeepSeekProvider(config);
      },
    ],
  ]);
}

export class ProviderRegistry {
  constructor({ config, factories } = {}) {
    if (!config || typeof config !== "object") {
      throw new TypeError("ProviderRegistry requires normalized config");
    }
    this.config = config;
    this.factories = defaultFactories();
    for (const [provider_id, factory] of Object.entries(factories || {})) {
      this.factories.set(provider_id, factory);
    }
    this.provider = null;
  }

  async initialize() {
    if (this.provider) {
      return this.provider;
    }
    const factory = this.factories.get(this.config.provider_id);
    if (!factory) {
      throw new ProviderError(
        providerErrorReasons.PROVIDER_NOT_FOUND,
        `Unknown AI Provider: ${this.config.provider_id}`,
        {
          provider: this.config.provider_id,
        }
      );
    }
    try {
      this.provider = assertProviderContract(await factory(this.config));
      return this.provider;
    } catch (error) {
      throw asProviderError(error, {
        reason: providerErrorReasons.INITIALIZATION_FAILED,
        message: `Failed to initialize Provider ${this.config.provider_id}`,
        provider: this.config.provider_id,
        model: this.config.model || null,
      });
    }
  }

  getSelectedProvider() {
    if (!this.provider) {
      throw new Error("ProviderRegistry has not been initialized");
    }
    return this.provider;
  }

  getMetadata() {
    if (!this.provider) {
      return {
        provider: this.config.provider_id,
        model: this.config.model || null,
        ready: false,
      };
    }
    return structuredClone(this.provider.getMetadata());
  }

  getLegacyAgent() {
    return this.provider && typeof this.provider.getAgent === "function"
      ? this.provider.getAgent()
      : null;
  }

  async close() {
    const provider = this.provider;
    this.provider = null;
    if (provider && typeof provider.close === "function") {
      await provider.close();
    }
  }
}
