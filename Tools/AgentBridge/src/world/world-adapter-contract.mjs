import {
  UUID_PATTERN,
  worldSnapshotSchema,
} from "../protocol/schemas.mjs";
import { createSchemaValidator } from "../tools/validator.mjs";
import {
  WorldAdapterError,
  worldAdapterErrorReasons,
} from "./world-adapter-errors.mjs";

export const ADAPTER_PROTOCOL_VERSION = "1.0";
export const DEFAULT_MAX_TOOL_CALLS = 16;
export const MAX_ADAPTER_TOOL_CALLS = 1000;

export const DEFAULT_WORLD_ADAPTER_CAPABILITIES = Object.freeze({
  supports_atomic_transactions: true,
  supports_dry_run: true,
  supports_undo: true,
  supports_idempotency: true,
  max_tool_calls: DEFAULT_MAX_TOOL_CALLS,
  supported_tools: Object.freeze([]),
});

const POLLUTION_KEYS = new Set(["__proto__", "prototype", "constructor"]);
const SNAPSHOT_INPUT_FIELDS = new Set([
  "request_id",
  "session_id",
  "world_id",
  "signal",
]);
const EXECUTE_INPUT_FIELDS = new Set([
  "request_id",
  "session_id",
  "world_id",
  "expected_revision",
  "dry_run",
  "atomic",
  "tool_calls",
  "signal",
]);
const UNDO_INPUT_FIELDS = new Set([
  "request_id",
  "session_id",
  "world_id",
  "expected_revision",
  "undo_token",
  "signal",
]);
const TOOL_CALL_FIELDS = new Set([
  "tool_call_id",
  "tool_name",
  "args",
]);
const UUID_REGEX = new RegExp(UUID_PATTERN);

const adapterCapabilitiesSchema = {
  type: "object",
  required: [
    "supports_atomic_transactions",
    "supports_dry_run",
    "supports_undo",
    "supports_idempotency",
    "max_tool_calls",
    "supported_tools",
  ],
  properties: {
    supports_atomic_transactions: { type: "boolean" },
    supports_dry_run: { type: "boolean" },
    supports_undo: { type: "boolean" },
    supports_idempotency: { type: "boolean" },
    max_tool_calls: {
      type: "integer",
      minimum: 1,
      maximum: MAX_ADAPTER_TOOL_CALLS,
    },
    supported_tools: {
      type: "array",
      minItems: 1,
      uniqueItems: true,
      items: { type: "string", minLength: 1, maxLength: 128 },
    },
  },
  additionalProperties: false,
};

const adapterErrorSchema = {
  anyOf: [
    { type: "null" },
    {
      type: "object",
      required: ["code", "message", "details", "retryable"],
      properties: {
        code: { type: "string", minLength: 1, maxLength: 128 },
        message: { type: "string", minLength: 1, maxLength: 2048 },
        details: { type: "object" },
        retryable: { type: "boolean" },
      },
      additionalProperties: true,
    },
  ],
};

const changeSchema = {
  type: "object",
  required: ["operation", "before", "after"],
  properties: {
    operation: { type: "string", minLength: 1, maxLength: 128 },
    entity_id: { type: "string", minLength: 1, maxLength: 128 },
    undo_token: { type: "string", minLength: 1, maxLength: 128 },
    property_name: { type: "string", minLength: 1, maxLength: 128 },
    before: {},
    after: {},
  },
  additionalProperties: true,
};

const toolResultSchema = {
  type: "object",
  required: [
    "ok",
    "request_id",
    "tool_call_id",
    "before_revision",
    "after_revision",
    "changes",
    "undo_token",
    "error",
  ],
  properties: {
    ok: { type: "boolean" },
    request_id: { type: "string", pattern: UUID_PATTERN },
    tool_call_id: {
      anyOf: [
        { type: "string", pattern: UUID_PATTERN },
        { type: "null" },
      ],
    },
    before_revision: {
      type: "integer",
      minimum: 0,
      maximum: Number.MAX_SAFE_INTEGER,
    },
    after_revision: {
      type: "integer",
      minimum: 0,
      maximum: Number.MAX_SAFE_INTEGER,
    },
    changes: {
      type: "array",
      items: changeSchema,
    },
    undo_token: {
      anyOf: [
        { type: "string", pattern: UUID_PATTERN },
        { type: "null" },
      ],
    },
    error: adapterErrorSchema,
    data: {},
    dry_run: { type: "boolean" },
    rolled_back: { type: "boolean" },
  },
  additionalProperties: true,
};

const executeResultSchema = {
  type: "object",
  required: [
    "ok",
    "request_id",
    "session_id",
    "world_id",
    "before_revision",
    "after_revision",
    "replayed",
    "tool_results",
    "changes",
    "undo_token",
    "error",
  ],
  properties: {
    ok: { type: "boolean" },
    request_id: { type: "string", pattern: UUID_PATTERN },
    session_id: { type: "string", pattern: UUID_PATTERN },
    world_id: { type: "string", minLength: 1, maxLength: 128 },
    before_revision: {
      type: "integer",
      minimum: 0,
      maximum: Number.MAX_SAFE_INTEGER,
    },
    after_revision: {
      type: "integer",
      minimum: 0,
      maximum: Number.MAX_SAFE_INTEGER,
    },
    replayed: { type: "boolean" },
    tool_results: {
      type: "array",
      items: toolResultSchema,
    },
    changes: {
      type: "array",
      items: changeSchema,
    },
    undo_token: {
      anyOf: [
        { type: "string", pattern: UUID_PATTERN },
        { type: "null" },
      ],
    },
    error: adapterErrorSchema,
    failed_tool_call_index: {
      anyOf: [
        {
          type: "integer",
          minimum: 0,
          maximum: Number.MAX_SAFE_INTEGER,
        },
        { type: "null" },
      ],
    },
    adapter_metadata: { type: "object" },
  },
  additionalProperties: true,
};

const undoResultSchema = {
  type: "object",
  required: [
    "ok",
    "request_id",
    "session_id",
    "world_id",
    "before_revision",
    "after_revision",
    "replayed",
    "changes",
    "undo_token",
    "error",
  ],
  properties: {
    ok: { type: "boolean" },
    request_id: { type: "string", pattern: UUID_PATTERN },
    session_id: { type: "string", pattern: UUID_PATTERN },
    world_id: { type: "string", minLength: 1, maxLength: 128 },
    before_revision: {
      type: "integer",
      minimum: 0,
      maximum: Number.MAX_SAFE_INTEGER,
    },
    after_revision: {
      type: "integer",
      minimum: 0,
      maximum: Number.MAX_SAFE_INTEGER,
    },
    replayed: { type: "boolean" },
    changes: {
      type: "array",
      items: changeSchema,
    },
    undo_token: {
      anyOf: [
        { type: "string", pattern: UUID_PATTERN },
        { type: "null" },
      ],
    },
    error: adapterErrorSchema,
    data: {},
    adapter_metadata: { type: "object" },
  },
  additionalProperties: true,
};

const healthResultSchema = {
  type: "object",
  required: [
    "ok",
    "adapter_protocol_version",
    "server_name",
    "server_version",
    "capabilities",
  ],
  properties: {
    ok: { type: "boolean" },
    adapter_protocol_version: { type: "string", minLength: 1 },
    server_name: { type: "string", minLength: 1, maxLength: 128 },
    server_version: { type: "string", minLength: 1, maxLength: 128 },
    capabilities: adapterCapabilitiesSchema,
    error: adapterErrorSchema,
  },
  additionalProperties: true,
};

const validator = createSchemaValidator();
const validateCapabilitiesSchema = validator.compile(adapterCapabilitiesSchema);
const validateSnapshotSchema = validator.compile(worldSnapshotSchema);
const validateExecuteResultSchema = validator.compile(executeResultSchema);
const validateUndoResultSchema = validator.compile(undoResultSchema);
const validateHealthResultSchema = validator.compile(healthResultSchema);

function validationFailure(message, phase, details = {}) {
  return new WorldAdapterError(
    worldAdapterErrorReasons.RESPONSE_INVALID,
    message,
    {
      protocol_version: ADAPTER_PROTOCOL_VERSION,
      phase,
      details,
    }
  );
}

function capabilityFailure(message, phase, details = {}) {
  return new WorldAdapterError(
    worldAdapterErrorReasons.CAPABILITY_INSUFFICIENT,
    message,
    {
      protocol_version: ADAPTER_PROTOCOL_VERSION,
      phase,
      details,
    }
  );
}

function knownToolNames(known_tools) {
  if (known_tools === undefined) {
    return null;
  }
  if (!Array.isArray(known_tools)) {
    throw new TypeError("known_tools must be an array of tool names");
  }
  return new Set(known_tools);
}

function assertKnownFields(value, fields, label) {
  if (!value || typeof value !== "object" || Array.isArray(value)) {
    throw new WorldAdapterError(
      worldAdapterErrorReasons.INVALID_ARGUMENT,
      `${label} must be an object`,
      {
        protocol_version: ADAPTER_PROTOCOL_VERSION,
        details: { field: label },
      }
    );
  }
  for (const key of Object.keys(value)) {
    if (!fields.has(key)) {
      throw new WorldAdapterError(
        worldAdapterErrorReasons.INVALID_ARGUMENT,
        `${label} contains an unknown field`,
        {
          protocol_version: ADAPTER_PROTOCOL_VERSION,
          details: { field: `${label}.${key}` },
        }
      );
    }
  }
}

function assertUuid(value, field) {
  if (typeof value !== "string" || !UUID_REGEX.test(value)) {
    throw new WorldAdapterError(
      worldAdapterErrorReasons.INVALID_ARGUMENT,
      `${field} must be a UUID`,
      {
        protocol_version: ADAPTER_PROTOCOL_VERSION,
        details: { field },
      }
    );
  }
}

function assertWorldId(value, field = "world_id") {
  if (typeof value !== "string" || value.length < 1 || value.length > 128) {
    throw new WorldAdapterError(
      worldAdapterErrorReasons.INVALID_ARGUMENT,
      `${field} must be a non-empty string`,
      {
        protocol_version: ADAPTER_PROTOCOL_VERSION,
        details: { field },
      }
    );
  }
}

function assertRevision(value, field) {
  if (!Number.isSafeInteger(value) || value < 0) {
    throw new WorldAdapterError(
      worldAdapterErrorReasons.INVALID_ARGUMENT,
      `${field} must be a non-negative safe integer`,
      {
        protocol_version: ADAPTER_PROTOCOL_VERSION,
        details: { field },
      }
    );
  }
}

function assertSignal(signal) {
  if (
    signal !== undefined &&
    (!(globalThis.AbortSignal) || !(signal instanceof AbortSignal))
  ) {
    throw new WorldAdapterError(
      worldAdapterErrorReasons.INVALID_ARGUMENT,
      "signal must be an AbortSignal",
      {
        protocol_version: ADAPTER_PROTOCOL_VERSION,
        details: { field: "signal" },
      }
    );
  }
}

export function assertJsonDto(value, label = "value", depth = 0) {
  if (depth > 64) {
    throw validationFailure(`${label} exceeds the maximum DTO depth`, "contract", {
      field: label,
    });
  }
  if (
    value === null ||
    typeof value === "string" ||
    typeof value === "boolean"
  ) {
    return value;
  }
  if (typeof value === "number") {
    if (!Number.isFinite(value)) {
      throw validationFailure(`${label} contains a non-finite number`, "contract", {
        field: label,
      });
    }
    return value;
  }
  if (Array.isArray(value)) {
    value.forEach((entry, index) =>
      assertJsonDto(entry, `${label}[${index}]`, depth + 1)
    );
    return value;
  }
  if (!value || typeof value !== "object") {
    throw validationFailure(`${label} is not JSON serializable`, "contract", {
      field: label,
    });
  }
  const prototype = Object.getPrototypeOf(value);
  if (prototype !== Object.prototype && prototype !== null) {
    throw validationFailure(`${label} must contain only plain objects`, "contract", {
      field: label,
    });
  }
  for (const [key, entry] of Object.entries(value)) {
    if (POLLUTION_KEYS.has(key)) {
      throw validationFailure(`${label} contains a forbidden key`, "contract", {
        field: `${label}.${key}`,
      });
    }
    assertJsonDto(entry, `${label}.${key}`, depth + 1);
  }
  return value;
}

function validateBaseInput(input, fields, label) {
  assertKnownFields(input, fields, label);
  assertUuid(input.request_id, "request_id");
  assertUuid(input.session_id, "session_id");
  assertWorldId(input.world_id);
  assertSignal(input.signal);
  const dto = { ...input };
  delete dto.signal;
  assertJsonDto(dto, label);
  return input;
}

export function validateGetSnapshotInput(input) {
  return validateBaseInput(input, SNAPSHOT_INPUT_FIELDS, "getSnapshot input");
}

export function validateExecuteTransactionInput(input) {
  validateBaseInput(
    input,
    EXECUTE_INPUT_FIELDS,
    "executeTransaction input"
  );
  assertRevision(input.expected_revision, "expected_revision");
  if (typeof input.dry_run !== "boolean") {
    throw new WorldAdapterError(
      worldAdapterErrorReasons.INVALID_ARGUMENT,
      "dry_run must be a boolean",
      { details: { field: "dry_run" } }
    );
  }
  if (typeof input.atomic !== "boolean") {
    throw new WorldAdapterError(
      worldAdapterErrorReasons.INVALID_ARGUMENT,
      "atomic must be a boolean",
      { details: { field: "atomic" } }
    );
  }
  if (!Array.isArray(input.tool_calls) || input.tool_calls.length === 0) {
    throw new WorldAdapterError(
      worldAdapterErrorReasons.INVALID_ARGUMENT,
      "tool_calls must be a non-empty array",
      { details: { field: "tool_calls" } }
    );
  }
  for (const [index, tool_call] of input.tool_calls.entries()) {
    assertKnownFields(
      tool_call,
      TOOL_CALL_FIELDS,
      `tool_calls[${index}]`
    );
    assertUuid(tool_call.tool_call_id, `tool_calls[${index}].tool_call_id`);
    if (
      typeof tool_call.tool_name !== "string" ||
      !tool_call.tool_name ||
      tool_call.tool_name.length > 128
    ) {
      throw new WorldAdapterError(
        worldAdapterErrorReasons.INVALID_ARGUMENT,
        "tool_name must be a non-empty string",
        { details: { field: `tool_calls[${index}].tool_name` } }
      );
    }
    assertJsonDto(tool_call.args, `tool_calls[${index}].args`);
    if (
      !tool_call.args ||
      typeof tool_call.args !== "object" ||
      Array.isArray(tool_call.args)
    ) {
      throw new WorldAdapterError(
        worldAdapterErrorReasons.INVALID_ARGUMENT,
        "ToolCall args must be an object",
        { details: { field: `tool_calls[${index}].args` } }
      );
    }
  }
  return input;
}

export function validateUndoInput(input) {
  validateBaseInput(input, UNDO_INPUT_FIELDS, "undo input");
  assertRevision(input.expected_revision, "expected_revision");
  if (
    input.undo_token !== null &&
    input.undo_token !== undefined
  ) {
    assertUuid(input.undo_token, "undo_token");
  }
  return input;
}

export function validateAdapterCapabilities(
  capabilities,
  { phase = "contract", known_tools } = {}
) {
  assertJsonDto(capabilities, "capabilities");
  if (!validateCapabilitiesSchema(capabilities)) {
    throw capabilityFailure(
      "World adapter capabilities failed contract validation",
      phase,
      { field: "capabilities" }
    );
  }
  const known_tool_names = knownToolNames(known_tools);
  if (known_tool_names) {
    for (const [index, tool_name] of capabilities.supported_tools.entries()) {
      if (!known_tool_names.has(tool_name)) {
        throw capabilityFailure(
          "World adapter capabilities contain an unknown tool",
          phase,
          {
            field: `capabilities.supported_tools[${index}]`,
            tool_name,
          }
        );
      }
    }
  }
  const supports_undo = capabilities.supports_undo;
  const advertises_undo = capabilities.supported_tools.includes(
    "history.undo"
  );
  if (supports_undo !== advertises_undo) {
    throw capabilityFailure(
      supports_undo
        ? "supports_undo requires history.undo in supported_tools"
        : "history.undo requires supports_undo=true",
      phase,
      { field: "capabilities.supports_undo", tool_name: "history.undo" }
    );
  }
  if (
    !capabilities.supports_atomic_transactions &&
    capabilities.max_tool_calls !== 1
  ) {
    throw capabilityFailure(
      "A non-atomic world adapter must declare max_tool_calls=1",
      phase,
      {
        field: "capabilities.max_tool_calls",
        max_tool_calls: capabilities.max_tool_calls,
      }
    );
  }
  return structuredClone(capabilities);
}

export function validateAdapterCapabilityRequest(
  capabilities,
  {
    tool_calls,
    dry_run = false,
    atomic = false,
    phase = "execute",
    known_tools,
  } = {}
) {
  const known_tool_names = knownToolNames(known_tools);
  const validated = validateAdapterCapabilities(capabilities, {
    phase,
    known_tools,
  });
  if (!Array.isArray(tool_calls) || tool_calls.length === 0) {
    throw capabilityFailure(
      "Capability validation requires at least one ToolCall",
      phase,
      { field: "tool_calls" }
    );
  }
  if (tool_calls.length > validated.max_tool_calls) {
    throw capabilityFailure(
      "ToolCall count exceeds the world adapter capability",
      phase,
      {
        tool_call_count: tool_calls.length,
        max_tool_calls: validated.max_tool_calls,
      }
    );
  }

  for (const [index, tool_call] of tool_calls.entries()) {
    const tool_name = tool_call?.tool_name ?? tool_call?.name;
    if (known_tool_names && !known_tool_names.has(tool_name)) {
      throw new WorldAdapterError(
        worldAdapterErrorReasons.UNKNOWN_TOOL,
        `Unknown tool: ${tool_name}`,
        {
          protocol_version: ADAPTER_PROTOCOL_VERSION,
          phase,
          details: { tool_call_index: index, tool_name },
        }
      );
    }
    if (tool_name === "history.undo" && !validated.supports_undo) {
      throw new WorldAdapterError(
        worldAdapterErrorReasons.UNDO_NOT_AVAILABLE,
        "World adapter does not support undo",
        {
          protocol_version: ADAPTER_PROTOCOL_VERSION,
          phase,
          details: { tool_call_index: index, tool_name },
        }
      );
    }
    if (!validated.supported_tools.includes(tool_name)) {
      throw capabilityFailure(
        "World adapter does not advertise the requested tool",
        phase,
        { tool_call_index: index, tool_name }
      );
    }
  }

  if (dry_run && !validated.supports_dry_run) {
    throw capabilityFailure(
      "World adapter does not support dry-run execution",
      phase,
      { field: "dry_run" }
    );
  }
  if (atomic && !validated.supports_atomic_transactions) {
    throw capabilityFailure(
      "World adapter does not support atomic transactions",
      phase,
      { field: "atomic" }
    );
  }
  if (
    tool_calls.length > 1 &&
    (!validated.supports_atomic_transactions || !atomic)
  ) {
    throw capabilityFailure(
      "Multiple ToolCalls require an atomic world adapter transaction",
      phase,
      {
        field: "tool_calls",
        tool_call_count: tool_calls.length,
      }
    );
  }
  return validated;
}

export function validateWorldSnapshot(
  snapshot,
  { expected_world_id, phase = "snapshot" } = {}
) {
  assertJsonDto(snapshot, "world snapshot");
  if (!validateSnapshotSchema(snapshot)) {
    throw validationFailure(
      "World snapshot failed contract validation",
      phase
    );
  }
  if (expected_world_id && snapshot.world_id !== expected_world_id) {
    throw new WorldAdapterError(
      worldAdapterErrorReasons.CORRELATION_MISMATCH,
      "World snapshot world_id does not match the request",
      {
        protocol_version: ADAPTER_PROTOCOL_VERSION,
        phase,
        details: {
          field: "world_id",
          expected: expected_world_id,
          received: snapshot.world_id,
        },
      }
    );
  }
  return structuredClone(snapshot);
}

function validateCorrelation(result, request, phase) {
  for (const field of ["request_id", "session_id", "world_id"]) {
    if (result[field] !== request[field]) {
      throw new WorldAdapterError(
        worldAdapterErrorReasons.CORRELATION_MISMATCH,
        `World adapter response ${field} does not match the request`,
        {
          protocol_version: ADAPTER_PROTOCOL_VERSION,
          phase,
          details: {
            field,
            expected: request[field],
            received: result[field],
          },
        }
      );
    }
  }
  if (result.after_revision < result.before_revision) {
    throw validationFailure(
      "World adapter response revision moved backward",
      phase,
      { field: "after_revision" }
    );
  }
}

export function validateExecuteTransactionResult(result, request) {
  assertJsonDto(result, "executeTransaction result");
  if (!validateExecuteResultSchema(result)) {
    throw validationFailure(
      "executeTransaction result failed contract validation",
      "execute"
    );
  }
  validateCorrelation(result, request, "execute");

  const call_index_by_id = new Map(
    request.tool_calls.map((tool_call, index) => [
      tool_call.tool_call_id,
      index,
    ])
  );
  const seen = new Set();
  let previous_index = -1;
  for (const tool_result of result.tool_results) {
    if (tool_result.request_id !== request.request_id) {
      throw new WorldAdapterError(
        worldAdapterErrorReasons.CORRELATION_MISMATCH,
        "ToolResult request_id does not match the transaction",
        {
          phase: "execute",
          details: { field: "tool_results[].request_id" },
        }
      );
    }
    const call_index = call_index_by_id.get(tool_result.tool_call_id);
    if (
      call_index === undefined ||
      seen.has(tool_result.tool_call_id) ||
      call_index < previous_index
    ) {
      throw new WorldAdapterError(
        worldAdapterErrorReasons.CORRELATION_MISMATCH,
        "ToolResult cannot be correlated to the ordered request ToolCalls",
        {
          phase: "execute",
          details: { tool_call_id: tool_result.tool_call_id },
        }
      );
    }
    if (
      tool_result.before_revision !== result.before_revision ||
      tool_result.after_revision !== result.after_revision
    ) {
      throw validationFailure(
        "ToolResult revision does not match the transaction",
        "execute",
        { tool_call_id: tool_result.tool_call_id }
      );
    }
    seen.add(tool_result.tool_call_id);
    previous_index = call_index;
  }
  if (result.ok && result.tool_results.length !== request.tool_calls.length) {
    throw validationFailure(
      "Successful transaction must return one ToolResult per ToolCall",
      "execute",
      {
        tool_call_count: request.tool_calls.length,
      }
    );
  }
  if (!result.ok && result.tool_results.length === 0) {
    throw validationFailure(
      "Failed transaction must identify at least one ToolResult",
      "execute"
    );
  }
  return structuredClone(result);
}

export function validateUndoResult(result, request) {
  assertJsonDto(result, "undo result");
  if (!validateUndoResultSchema(result)) {
    throw validationFailure("undo result failed contract validation", "undo");
  }
  validateCorrelation(result, request, "undo");
  return structuredClone(result);
}

export function validateHealthResult(result) {
  assertJsonDto(result, "health result");
  if (!validateHealthResultSchema(result)) {
    throw validationFailure("health result failed contract validation", "health");
  }
  validateAdapterCapabilities(result.capabilities, { phase: "health" });
  return structuredClone(result);
}

/**
 * @typedef {object} WorldAdapterCapabilities
 * @property {boolean} supports_atomic_transactions
 * @property {boolean} supports_dry_run
 * @property {boolean} supports_undo
 * @property {boolean} supports_idempotency
 * @property {number} max_tool_calls
 * @property {string[]} supported_tools
 *
 * @typedef {object} WorldAdapter
 * @property {string} name
 * @property {string} protocolVersion
 * @property {WorldAdapterCapabilities} capabilities
 * @property {(input: object) => Promise<object>} getSnapshot
 * @property {(input: object) => Promise<object>} executeTransaction
 * @property {(input: object) => Promise<object>} undo
 * @property {() => Promise<object>} health
 * @property {() => Promise<void>} close
 * @property {() => object} getMetadata
 */
export function assertWorldAdapterContract(adapter) {
  if (!adapter || typeof adapter !== "object") {
    throw new TypeError("WorldAdapter must be an object");
  }
  if (typeof adapter.name !== "string" || !adapter.name) {
    throw new TypeError("WorldAdapter name is required");
  }
  if (adapter.protocolVersion !== ADAPTER_PROTOCOL_VERSION) {
    throw new TypeError(
      `WorldAdapter protocolVersion must be ${ADAPTER_PROTOCOL_VERSION}`
    );
  }
  validateAdapterCapabilities(adapter.capabilities, {
    known_tools: adapter.tool_registry
      ?.listDefinitions()
      .map((definition) => definition.name),
  });
  for (const method of [
    "getSnapshot",
    "executeTransaction",
    "undo",
    "health",
    "close",
    "getMetadata",
  ]) {
    if (typeof adapter[method] !== "function") {
      throw new TypeError(`WorldAdapter ${method}() is required`);
    }
  }
  const metadata = adapter.getMetadata();
  assertJsonDto(metadata, "adapter metadata");
  if (
    metadata.adapter !== adapter.name ||
    metadata.adapter_protocol_version !== adapter.protocolVersion
  ) {
    throw new TypeError("WorldAdapter metadata identity is inconsistent");
  }
  return adapter;
}
