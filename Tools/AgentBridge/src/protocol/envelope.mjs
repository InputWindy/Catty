import { randomUUID } from "node:crypto";
import { AgentError } from "./errors.mjs";
import { PROTOCOL_VERSION, UUID_PATTERN } from "./schemas.mjs";

const uuid_regex = new RegExp(UUID_PATTERN);

export function createEnvelope({
  request_id = randomUUID(),
  session_id,
  world_id,
  type,
  timestamp_ms = Date.now(),
  payload = {},
}) {
  return {
    protocol_version: PROTOCOL_VERSION,
    request_id,
    session_id,
    world_id,
    type,
    timestamp_ms,
    payload,
  };
}

export function assertProtocolVersion(protocol_version) {
  if (protocol_version !== PROTOCOL_VERSION) {
    throw new AgentError(
      "UNSUPPORTED_PROTOCOL_VERSION",
      `Unsupported protocol_version: ${protocol_version}`,
      {
        expected: PROTOCOL_VERSION,
        received: protocol_version ?? null,
      }
    );
  }
}

export function assertUuid(value, field_name) {
  if (typeof value !== "string" || !uuid_regex.test(value)) {
    throw new AgentError("INVALID_REQUEST", `${field_name} must be a UUID`, {
      field: field_name,
    });
  }
}

export function validateEnvelope(envelope) {
  if (!envelope || typeof envelope !== "object" || Array.isArray(envelope)) {
    throw new AgentError("INVALID_REQUEST", "Request envelope must be an object");
  }
  assertProtocolVersion(envelope.protocol_version);
  assertUuid(envelope.request_id, "request_id");
  assertUuid(envelope.session_id, "session_id");
  assertUuid(envelope.world_id, "world_id");
  if (typeof envelope.type !== "string" || envelope.type.length === 0) {
    throw new AgentError("INVALID_REQUEST", "type is required", {
      field: "type",
    });
  }
  if (
    !Number.isSafeInteger(envelope.timestamp_ms) ||
    envelope.timestamp_ms < 0
  ) {
    throw new AgentError(
      "INVALID_REQUEST",
      "timestamp_ms must be a non-negative safe integer",
      { field: "timestamp_ms" }
    );
  }
  if (
    !envelope.payload ||
    typeof envelope.payload !== "object" ||
    Array.isArray(envelope.payload)
  ) {
    throw new AgentError("INVALID_REQUEST", "payload must be an object", {
      field: "payload",
    });
  }
  return envelope;
}

