import { AgentError } from "../../protocol/errors.mjs";

export const providerErrorReasons = Object.freeze({
  CONFIGURATION_MISSING: "configuration_missing",
  PROVIDER_NOT_FOUND: "provider_not_found",
  API_KEY_MISSING: "api_key_missing",
  INITIALIZATION_FAILED: "initialization_failed",
  REQUEST_TIMEOUT: "request_timeout",
  REQUEST_CANCELLED: "request_cancelled",
  CONNECTION_FAILED: "connection_failed",
  HTTP_ERROR: "http_error",
  RESPONSE_INVALID_JSON: "response_invalid_json",
  RESPONSE_INVALID: "response_invalid",
  TOOL_ARGUMENTS_INVALID: "tool_arguments_invalid",
  UNKNOWN_TOOL: "unknown_tool",
  TOOL_CALL_LIMIT_EXCEEDED: "tool_call_limit_exceeded",
  FINALIZATION_TOOL_CALL: "finalization_tool_call",
});

const SAFE_DETAIL_KEYS = new Set([
  "provider",
  "model",
  "phase",
  "provider_reason",
  "http_status",
  "attempt_count",
  "tool_call_index",
  "tool_call_count",
  "max_tool_calls",
  "retry_after_ms",
  "timeout",
  "cancelled",
  "field",
]);

function safeDetails(details = {}) {
  return Object.fromEntries(
    Object.entries(details).filter(([key]) => SAFE_DETAIL_KEYS.has(key))
  );
}

export class ProviderError extends Error {
  constructor(
    reason,
    message,
    {
      provider = null,
      model = null,
      phase = null,
      http_status = null,
      attempt_count = 0,
      retryable = false,
      timeout = false,
      cancelled = false,
      details = {},
      cause,
    } = {}
  ) {
    super(message, cause === undefined ? undefined : { cause });
    this.name = "ProviderError";
    this.reason = reason;
    this.provider = provider;
    this.model = model;
    this.phase = phase;
    this.http_status = http_status;
    this.attempt_count = attempt_count;
    this.retryable = retryable;
    this.timeout = timeout;
    this.cancelled = cancelled;
    this.details = safeDetails(details);
  }

  toSafeDetails() {
    return {
      provider: this.provider,
      model: this.model,
      phase: this.phase,
      provider_reason: this.reason,
      http_status: this.http_status,
      attempt_count: this.attempt_count,
      timeout: this.timeout,
      cancelled: this.cancelled,
      ...this.details,
    };
  }
}

export function asProviderError(
  error,
  {
    reason = providerErrorReasons.INITIALIZATION_FAILED,
    message = "AI Provider operation failed",
    provider = null,
    model = null,
    phase = null,
  } = {}
) {
  if (error instanceof ProviderError) {
    return error;
  }
  return new ProviderError(reason, message, {
    provider,
    model,
    phase,
    cause: error,
  });
}

export function providerErrorToAgentError(error) {
  const provider_error = asProviderError(error);
  let code = "EXECUTION_FAILED";
  if (provider_error.reason === providerErrorReasons.REQUEST_TIMEOUT) {
    code = "TIMEOUT";
  } else if (
    provider_error.reason === providerErrorReasons.RESPONSE_INVALID ||
    provider_error.reason === providerErrorReasons.RESPONSE_INVALID_JSON ||
    provider_error.reason === providerErrorReasons.TOOL_ARGUMENTS_INVALID ||
    provider_error.reason === providerErrorReasons.UNKNOWN_TOOL ||
    provider_error.reason === providerErrorReasons.TOOL_CALL_LIMIT_EXCEEDED ||
    provider_error.reason === providerErrorReasons.FINALIZATION_TOOL_CALL
  ) {
    code = "MODEL_OUTPUT_INVALID";
  } else if (
    provider_error.http_status === 401 ||
    provider_error.http_status === 403
  ) {
    code = "PERMISSION_DENIED";
  }
  return new AgentError(
    code,
    provider_error.message,
    provider_error.toSafeDetails(),
    provider_error.retryable
  );
}
