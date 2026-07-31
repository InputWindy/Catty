import { AgentError } from "../protocol/errors.mjs";

export const worldAdapterErrorReasons = Object.freeze({
  CONFIGURATION_ERROR: "configuration_error",
  INVALID_BASE_URL: "invalid_base_url",
  NON_LOOPBACK_REJECTED: "non_loopback_rejected",
  AUTH_REQUIRED: "auth_required",
  ADAPTER_UNAVAILABLE: "adapter_unavailable",
  PROTOCOL_VERSION_INCOMPATIBLE: "protocol_version_incompatible",
  CAPABILITY_INSUFFICIENT: "capability_insufficient",
  REQUEST_TIMEOUT: "request_timeout",
  REQUEST_CANCELLED: "request_cancelled",
  CONNECTION_FAILED: "connection_failed",
  HTTP_ERROR: "http_error",
  RESPONSE_INVALID_JSON: "response_invalid_json",
  RESPONSE_INVALID: "response_invalid",
  CORRELATION_MISMATCH: "correlation_mismatch",
  REVISION_CONFLICT: "revision_conflict",
  ENTITY_NOT_FOUND: "entity_not_found",
  UNKNOWN_TOOL: "unknown_tool",
  INVALID_ARGUMENT: "invalid_argument",
  TRANSACTION_FAILED: "transaction_failed",
  UNDO_NOT_AVAILABLE: "undo_not_available",
});

const SAFE_DETAIL_KEYS = new Set([
  "adapter",
  "adapter_reason",
  "adapter_protocol_version",
  "phase",
  "http_status",
  "timeout",
  "cancelled",
  "field",
  "expected",
  "received",
  "expected_revision",
  "current_revision",
  "request_id",
  "session_id",
  "world_id",
  "tool_call_id",
  "tool_call_index",
  "tool_name",
  "max_tool_calls",
  "tool_call_count",
  "endpoint_origin",
]);

function safeDetails(details = {}) {
  return Object.fromEntries(
    Object.entries(details).filter(([key]) => SAFE_DETAIL_KEYS.has(key))
  );
}

export class WorldAdapterError extends Error {
  constructor(
    reason,
    message,
    {
      adapter = null,
      protocol_version = null,
      phase = null,
      http_status = null,
      retryable = false,
      timeout = false,
      cancelled = false,
      details = {},
      cause,
    } = {}
  ) {
    super(message, cause === undefined ? undefined : { cause });
    this.name = "WorldAdapterError";
    this.reason = reason;
    this.adapter = adapter;
    this.protocol_version = protocol_version;
    this.phase = phase;
    this.http_status = http_status;
    this.retryable = retryable;
    this.timeout = timeout;
    this.cancelled = cancelled;
    this.details = safeDetails(details);
  }

  toSafeDetails() {
    return {
      adapter: this.adapter,
      adapter_reason: this.reason,
      adapter_protocol_version: this.protocol_version,
      phase: this.phase,
      http_status: this.http_status,
      timeout: this.timeout,
      cancelled: this.cancelled,
      ...this.details,
    };
  }
}

export function asWorldAdapterError(
  error,
  {
    reason = worldAdapterErrorReasons.TRANSACTION_FAILED,
    message = "World adapter operation failed",
    adapter = null,
    protocol_version = null,
    phase = null,
  } = {}
) {
  if (error instanceof WorldAdapterError) {
    return error;
  }
  return new WorldAdapterError(reason, message, {
    adapter,
    protocol_version,
    phase,
    cause: error,
  });
}

export function worldAdapterErrorToAgentError(error) {
  const adapter_error = asWorldAdapterError(error);
  let code = "EXECUTION_FAILED";

  if (
    adapter_error.reason === worldAdapterErrorReasons.CONFIGURATION_ERROR ||
    adapter_error.reason === worldAdapterErrorReasons.INVALID_BASE_URL ||
    adapter_error.reason === worldAdapterErrorReasons.NON_LOOPBACK_REJECTED ||
    adapter_error.reason === worldAdapterErrorReasons.AUTH_REQUIRED
  ) {
    code = "INVALID_REQUEST";
  } else if (
    adapter_error.reason === worldAdapterErrorReasons.REQUEST_TIMEOUT
  ) {
    code = "TIMEOUT";
  } else if (
    adapter_error.reason === worldAdapterErrorReasons.REVISION_CONFLICT
  ) {
    code = "REVISION_CONFLICT";
  } else if (
    adapter_error.reason === worldAdapterErrorReasons.ENTITY_NOT_FOUND
  ) {
    code = "ENTITY_NOT_FOUND";
  } else if (
    adapter_error.reason === worldAdapterErrorReasons.UNKNOWN_TOOL
  ) {
    code = "UNKNOWN_TOOL";
  } else if (
    adapter_error.reason === worldAdapterErrorReasons.INVALID_ARGUMENT
  ) {
    code = "INVALID_ARGUMENT";
  } else if (
    adapter_error.reason === worldAdapterErrorReasons.UNDO_NOT_AVAILABLE
  ) {
    code = "UNDO_NOT_AVAILABLE";
  } else if (
    adapter_error.http_status === 401 ||
    adapter_error.http_status === 403
  ) {
    code = "PERMISSION_DENIED";
  } else if (adapter_error.http_status === 409) {
    code = "REVISION_CONFLICT";
  }

  return new AgentError(
    code,
    adapter_error.message,
    adapter_error.toSafeDetails(),
    adapter_error.retryable
  );
}
