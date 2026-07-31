import { AgentError } from "../protocol/errors.mjs";
import { UUID_PATTERN } from "../protocol/schemas.mjs";
import {
  assertNormalizedMessages,
  assertSafePlainObject,
  isPlainObject,
} from "./normalized-messages.mjs";

const UUID_REGEX = new RegExp(UUID_PATTERN);
const USAGE_FIELDS = [
  "input_tokens",
  "output_tokens",
  "total_tokens",
  "cached_input_tokens",
];
const SENSITIVE_METADATA_KEY =
  /authorization|api[_-]?key|secret|password|credential|reasoning_content/i;

export const EMPTY_USAGE = Object.freeze({
  input_tokens: null,
  output_tokens: null,
  total_tokens: null,
  cached_input_tokens: null,
});

export const DEFAULT_PROVIDER_CAPABILITIES = Object.freeze({
  supports_tools: true,
  supports_finalization: false,
  supports_streaming: false,
  supports_thinking: false,
});

/**
 * @typedef {object} ProviderCapabilities
 * @property {boolean} supports_tools
 * @property {boolean} supports_finalization
 * @property {boolean} supports_streaming
 * @property {boolean} supports_thinking
 *
 * @typedef {object} ProviderUsage
 * @property {number|null} input_tokens
 * @property {number|null} output_tokens
 * @property {number|null} total_tokens
 * @property {number|null} cached_input_tokens
 *
 * @typedef {object} ProviderPlanInput
 * @property {string} request_id
 * @property {string} session_id
 * @property {string} user_message
 * @property {import("./normalized-messages.mjs").NormalizedMessage[]} normalized_messages
 * @property {object} world_snapshot
 * @property {object} session_context
 * @property {object[]} tool_definitions
 * @property {AbortSignal|undefined} signal
 *
 * @typedef {object} ProviderOutput
 * @property {string} provider
 * @property {string} model
 * @property {string} assistant_message
 * @property {Array<{tool_call_id: string, tool_name: string, args: object}>} tool_calls
 * @property {string|null} finish_reason
 * @property {ProviderUsage} usage
 * @property {object} provider_metadata
 */

function invalid(message, details = {}) {
  return new AgentError("MODEL_OUTPUT_INVALID", message, details);
}

export function assertProviderCapabilities(capabilities) {
  if (!isPlainObject(capabilities)) {
    throw new TypeError("Provider capabilities must be an object");
  }
  for (const field of Object.keys(DEFAULT_PROVIDER_CAPABILITIES)) {
    if (typeof capabilities[field] !== "boolean") {
      throw new TypeError(`Provider capability ${field} must be a boolean`);
    }
  }
  return capabilities;
}

export function assertProviderContract(provider) {
  if (!isPlainObject(provider) && typeof provider !== "object") {
    throw new TypeError("Provider must be an object");
  }
  if (typeof provider.name !== "string" || !provider.name) {
    throw new TypeError("Provider name is required");
  }
  if (typeof provider.model !== "string" || !provider.model) {
    throw new TypeError("Provider model is required");
  }
  assertProviderCapabilities(provider.capabilities);
  if (typeof provider.plan !== "function") {
    throw new TypeError("Provider plan() is required");
  }
  if (
    provider.capabilities.supports_finalization &&
    typeof provider.finalize !== "function"
  ) {
    throw new TypeError(
      "Provider finalize() is required when finalization is supported"
    );
  }
  if (provider.close !== undefined && typeof provider.close !== "function") {
    throw new TypeError("Provider close must be a function");
  }
  if (typeof provider.getMetadata !== "function") {
    throw new TypeError("Provider getMetadata() is required");
  }
  return provider;
}

export function assertProviderPlanInput(input) {
  if (!isPlainObject(input)) {
    throw new TypeError("Provider plan input must be an object");
  }
  for (const field of ["request_id", "session_id", "user_message"]) {
    if (typeof input[field] !== "string" || !input[field]) {
      throw new TypeError(`Provider plan input ${field} is required`);
    }
  }
  assertNormalizedMessages(input.normalized_messages);
  assertSafePlainObject(input.world_snapshot, "world_snapshot");
  assertSafePlainObject(input.session_context, "session_context");
  if (!Array.isArray(input.tool_definitions)) {
    throw new TypeError("Provider plan input tool_definitions must be an array");
  }
  if (
    input.signal !== undefined &&
    !(input.signal instanceof AbortSignal)
  ) {
    throw new TypeError("Provider plan input signal must be an AbortSignal");
  }
  return input;
}

export function normalizeUsage(usage = {}) {
  if (!isPlainObject(usage)) {
    throw invalid("Provider usage must be an object");
  }
  const normalized = {};
  for (const field of USAGE_FIELDS) {
    const value = usage[field] ?? null;
    if (
      value !== null &&
      (!Number.isSafeInteger(value) || value < 0)
    ) {
      throw invalid(`Provider usage ${field} must be null or a non-negative integer`, {
        field: `usage.${field}`,
      });
    }
    normalized[field] = value;
  }
  return normalized;
}

function assertSafeMetadata(value, path = "provider_metadata") {
  if (!isPlainObject(value)) {
    throw invalid("provider_metadata must be a plain object");
  }
  for (const [key, entry] of Object.entries(value)) {
    if (SENSITIVE_METADATA_KEY.test(key)) {
      throw invalid("provider_metadata contains a forbidden field", {
        field: `${path}.${key}`,
      });
    }
    if (Array.isArray(entry)) {
      entry.forEach((item, index) => {
        if (item && typeof item === "object") {
          assertSafeMetadata(item, `${path}.${key}[${index}]`);
        }
      });
    } else if (entry && typeof entry === "object") {
      assertSafeMetadata(entry, `${path}.${key}`);
    }
  }
  return value;
}

export function validateProviderOutput(
  output,
  {
    expected_provider,
    expected_model,
    max_tool_calls = 16,
    phase = "plan",
  } = {}
) {
  if (!isPlainObject(output)) {
    throw invalid("Provider output must be an object");
  }
  if (
    typeof output.provider !== "string" ||
    !output.provider ||
    typeof output.model !== "string" ||
    !output.model
  ) {
    throw invalid("Provider output must contain provider and model");
  }
  if (expected_provider && output.provider !== expected_provider) {
    throw invalid("Provider output identity does not match selected provider");
  }
  if (expected_model && output.model !== expected_model) {
    throw invalid("Provider output model does not match configured model");
  }
  if (
    typeof output.assistant_message !== "string" ||
    !Array.isArray(output.tool_calls)
  ) {
    throw invalid(
      "Provider output must contain assistant_message and tool_calls"
    );
  }
  if (output.tool_calls.length > max_tool_calls) {
    throw invalid("Provider returned too many tool calls", {
      provider_reason: "tool_call_limit_exceeded",
      tool_call_count: output.tool_calls.length,
      max_tool_calls,
    });
  }
  if (phase === "finalize" && output.tool_calls.length > 0) {
    throw invalid("Provider finalization returned a ToolCall", {
      provider_reason: "finalization_tool_call",
      tool_call_count: output.tool_calls.length,
    });
  }
  for (const [index, tool_call] of output.tool_calls.entries()) {
    if (!isPlainObject(tool_call)) {
      throw invalid("Provider returned an invalid ToolCall", {
        tool_call_index: index,
      });
    }
    if (
      typeof tool_call.tool_call_id !== "string" ||
      !UUID_REGEX.test(tool_call.tool_call_id) ||
      typeof tool_call.tool_name !== "string" ||
      !tool_call.tool_name
    ) {
      throw invalid("Provider ToolCall is missing a valid id or name", {
        tool_call_index: index,
      });
    }
    assertSafePlainObject(tool_call.args, `tool_calls[${index}].args`);
  }
  if (
    output.finish_reason !== null &&
    typeof output.finish_reason !== "string"
  ) {
    throw invalid("Provider finish_reason must be a string or null");
  }
  output.usage = normalizeUsage(output.usage);
  assertSafeMetadata(output.provider_metadata);
  return output;
}

export function createProviderOutput({
  provider,
  model,
  assistant_message = "",
  tool_calls = [],
  finish_reason = null,
  usage = EMPTY_USAGE,
  provider_metadata = {},
}) {
  return validateProviderOutput(
    {
      provider,
      model,
      assistant_message,
      tool_calls,
      finish_reason,
      usage: { ...usage },
      provider_metadata,
    },
    {
      expected_provider: provider,
      expected_model: model,
      max_tool_calls: Number.MAX_SAFE_INTEGER,
    }
  );
}
