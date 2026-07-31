import { OpenAICompatibleProvider } from "../openai-compatible-provider.mjs";
import {
  ProviderError,
  providerErrorReasons,
} from "../provider-errors.mjs";

export const DEEPSEEK_DEFAULTS = Object.freeze({
  provider_id: "deepseek",
  base_url: "https://api.deepseek.com",
  model: "deepseek-v4-flash",
  thinking: "disabled",
});

export function createDeepSeekProvider(config) {
  if (config.thinking && config.thinking !== "disabled") {
    throw new ProviderError(
      providerErrorReasons.CONFIGURATION_MISSING,
      "DeepSeek thinking mode is not supported by Agent Core v0.3",
      { provider: "deepseek", details: { field: "MAHO_AI_THINKING" } }
    );
  }
  if (!config.api_key) {
    throw new ProviderError(
      providerErrorReasons.API_KEY_MISSING,
      "MAHO_AI_API_KEY or DEEPSEEK_API_KEY is required for Provider deepseek",
      { provider: "deepseek" }
    );
  }
  return new OpenAICompatibleProvider({
    ...config,
    provider_id: DEEPSEEK_DEFAULTS.provider_id,
    base_url: config.base_url || DEEPSEEK_DEFAULTS.base_url,
    model: config.model || DEEPSEEK_DEFAULTS.model,
    request_extras: {
      thinking: {
        type: "disabled",
      },
    },
  });
}
