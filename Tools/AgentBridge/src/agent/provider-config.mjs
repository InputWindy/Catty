import path from "node:path";
import {
  ProviderError,
  providerErrorReasons,
} from "./providers/provider-errors.mjs";

export const PROVIDER_IDS = Object.freeze([
  "mock",
  "deepseek",
  "openai-compatible",
  "cursor",
]);

export const providerConfigDefaults = Object.freeze({
  timeout_ms: 30_000,
  max_retries: 1,
  temperature: 0,
  finalize: true,
  max_tool_calls: 16,
  mock_model: "mock-deterministic-v0.2",
  cursor_model: "composer-2.5",
});

function configError(reason, message, details = {}) {
  return new ProviderError(reason, message, { details });
}

function trimmed(value) {
  return String(value ?? "").trim();
}

function parseInteger(env, name, default_value, { minimum, maximum }) {
  const raw = trimmed(env[name]);
  if (!raw) {
    return default_value;
  }
  const value = Number(raw);
  if (
    !Number.isSafeInteger(value) ||
    value < minimum ||
    value > maximum
  ) {
    throw configError(
      providerErrorReasons.CONFIGURATION_MISSING,
      `${name} must be an integer from ${minimum} through ${maximum}`,
      { field: name }
    );
  }
  return value;
}

function parseNumber(env, name, default_value, { minimum, maximum }) {
  const raw = trimmed(env[name]);
  if (!raw) {
    return default_value;
  }
  const value = Number(raw);
  if (!Number.isFinite(value) || value < minimum || value > maximum) {
    throw configError(
      providerErrorReasons.CONFIGURATION_MISSING,
      `${name} must be a number from ${minimum} through ${maximum}`,
      { field: name }
    );
  }
  return value;
}

function parseBoolean(env, name, default_value) {
  const raw = trimmed(env[name]).toLowerCase();
  if (!raw) {
    return default_value;
  }
  if (["1", "true", "yes", "on"].includes(raw)) {
    return true;
  }
  if (["0", "false", "no", "off"].includes(raw)) {
    return false;
  }
  throw configError(
    providerErrorReasons.CONFIGURATION_MISSING,
    `${name} must be a boolean`,
    { field: name }
  );
}

export function selectProviderId({ env = process.env, legacy_api_key = "" } = {}) {
  if (env.CATTY_AGENT_MOCK === "1") {
    return "mock";
  }
  const explicit = trimmed(env.CATTY_AI_PROVIDER).toLowerCase();
  if (explicit) {
    if (!PROVIDER_IDS.includes(explicit)) {
      throw configError(
        providerErrorReasons.PROVIDER_NOT_FOUND,
        `Unknown AI Provider: ${explicit}`,
        { provider: explicit }
      );
    }
    return explicit;
  }
  if (trimmed(legacy_api_key || env.CURSOR_API_KEY)) {
    return "cursor";
  }
  return "mock";
}

function requireValue(value, variable_name, provider) {
  if (!value) {
    const reason =
      variable_name.includes("API_KEY")
        ? providerErrorReasons.API_KEY_MISSING
        : providerErrorReasons.CONFIGURATION_MISSING;
    throw new ProviderError(
      reason,
      `${variable_name} is required for Provider ${provider}`,
      {
        provider,
        details: { field: variable_name },
      }
    );
  }
  return value;
}

export function resolveProviderConfig({
  env = process.env,
  legacy_api_key = "",
  cwd = process.cwd(),
} = {}) {
  const provider_id = selectProviderId({ env, legacy_api_key });
  const timeout_ms = parseInteger(
    env,
    "CATTY_AI_TIMEOUT_MS",
    providerConfigDefaults.timeout_ms,
    { minimum: 1, maximum: 300_000 }
  );
  const max_retries = parseInteger(
    env,
    "CATTY_AI_MAX_RETRIES",
    providerConfigDefaults.max_retries,
    { minimum: 0, maximum: 10 }
  );
  const temperature = parseNumber(
    env,
    "CATTY_AI_TEMPERATURE",
    providerConfigDefaults.temperature,
    { minimum: 0, maximum: 2 }
  );
  const finalize = parseBoolean(
    env,
    "CATTY_AI_FINALIZE",
    providerConfigDefaults.finalize
  );
  const max_tool_calls = parseInteger(
    env,
    "CATTY_AI_MAX_TOOL_CALLS",
    providerConfigDefaults.max_tool_calls,
    { minimum: 1, maximum: 100 }
  );
  const thinking = trimmed(env.CATTY_AI_THINKING).toLowerCase();
  if (thinking && !["disabled", "false", "0", "off"].includes(thinking)) {
    throw configError(
      providerErrorReasons.CONFIGURATION_MISSING,
      "Thinking mode is not supported by Agent Core v0.3",
      { field: "CATTY_AI_THINKING" }
    );
  }

  const common = {
    provider_id,
    timeout_ms,
    max_retries,
    temperature,
    finalize,
    max_tool_calls,
    thinking: "disabled",
    cwd: path.resolve(cwd),
  };

  if (provider_id === "mock") {
    return {
      ...common,
      model: trimmed(env.CATTY_AI_MODEL) || providerConfigDefaults.mock_model,
      base_url: null,
      api_key: "",
      finalize: false,
      real: false,
    };
  }
  if (provider_id === "cursor") {
    return {
      ...common,
      model: trimmed(env.CATTY_AI_MODEL) || providerConfigDefaults.cursor_model,
      base_url: null,
      api_key: requireValue(
        trimmed(legacy_api_key || env.CURSOR_API_KEY),
        "CURSOR_API_KEY",
        provider_id
      ),
      finalize: false,
      real: true,
    };
  }
  if (provider_id === "openai-compatible") {
    return {
      ...common,
      model: requireValue(
        trimmed(env.CATTY_AI_MODEL),
        "CATTY_AI_MODEL",
        provider_id
      ),
      base_url: requireValue(
        trimmed(env.CATTY_AI_BASE_URL),
        "CATTY_AI_BASE_URL",
        provider_id
      ),
      api_key: requireValue(
        trimmed(env.CATTY_AI_API_KEY),
        "CATTY_AI_API_KEY",
        provider_id
      ),
      real: true,
    };
  }

  return {
    ...common,
    model: trimmed(env.CATTY_AI_MODEL),
    base_url: trimmed(env.CATTY_AI_BASE_URL),
    api_key:
      trimmed(env.CATTY_AI_API_KEY) || trimmed(env.DEEPSEEK_API_KEY),
    real: true,
  };
}
