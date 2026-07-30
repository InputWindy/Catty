export const errorCodes = Object.freeze([
  "INVALID_REQUEST",
  "UNSUPPORTED_PROTOCOL_VERSION",
  "UNKNOWN_SESSION",
  "UNKNOWN_WORLD",
  "UNKNOWN_TOOL",
  "INVALID_ARGUMENT",
  "ENTITY_NOT_FOUND",
  "REVISION_CONFLICT",
  "PERMISSION_DENIED",
  "MODEL_OUTPUT_INVALID",
  "EXECUTION_FAILED",
  "UNDO_NOT_AVAILABLE",
  "REQUEST_TOO_LARGE",
  "BUSY",
  "TIMEOUT",
  "INTERNAL_ERROR",
]);

export class AgentError extends Error {
  constructor(code, message, details = {}, retryable = false) {
    super(message);
    this.name = "AgentError";
    this.code = code;
    this.details = details;
    this.retryable = retryable;
  }

  toJSON() {
    return {
      code: this.code,
      message: this.message,
      details: this.details,
      retryable: this.retryable,
    };
  }
}

export function asAgentError(error, fallback_code = "INTERNAL_ERROR") {
  if (error instanceof AgentError) {
    return error;
  }
  return new AgentError(
    fallback_code,
    error?.message || String(error),
    {},
    false
  );
}

export function errorHttpStatus(error) {
  switch (error.code) {
    case "UNKNOWN_SESSION":
    case "UNKNOWN_WORLD":
    case "UNKNOWN_TOOL":
    case "ENTITY_NOT_FOUND":
      return 404;
    case "REVISION_CONFLICT":
    case "BUSY":
      return 409;
    case "REQUEST_TOO_LARGE":
      return 413;
    case "PERMISSION_DENIED":
      return 403;
    case "TIMEOUT":
      return 504;
    case "INTERNAL_ERROR":
    case "EXECUTION_FAILED":
      return 500;
    default:
      return 400;
  }
}

