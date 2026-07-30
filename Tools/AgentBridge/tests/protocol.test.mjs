import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import test from "node:test";
import {
  createEnvelope,
  validateEnvelope,
} from "../src/protocol/envelope.mjs";
import { AgentError, errorCodes } from "../src/protocol/errors.mjs";

test("protocol envelope uses version 1.0 and snake_case fields", () => {
  const envelope = createEnvelope({
    session_id: randomUUID(),
    world_id: randomUUID(),
    type: "tools.execute",
    payload: { tool_calls: [] },
  });

  assert.equal(validateEnvelope(envelope), envelope);
  assert.equal(envelope.protocol_version, "1.0");
  assert.match(envelope.request_id, /^[0-9a-f-]{36}$/);
  assert.equal("requestId" in envelope, false);
});

test("protocol rejects unsupported versions with a stable error", () => {
  const envelope = createEnvelope({
    session_id: randomUUID(),
    world_id: randomUUID(),
    type: "tools.execute",
  });
  envelope.protocol_version = "2.0";

  assert.throws(
    () => validateEnvelope(envelope),
    (error) =>
      error instanceof AgentError &&
      error.code === "UNSUPPORTED_PROTOCOL_VERSION" &&
      error.retryable === false
  );
});

test("all required error codes are defined", () => {
  assert.deepEqual(errorCodes, [
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
});

