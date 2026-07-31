export const PROTOCOL_VERSION = "1.0";

export const UUID_PATTERN =
  "^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-8][0-9a-fA-F]{3}-[89abAB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}$";

export const POSITION_LIMIT = 100_000;
export const ROTATION_LIMIT = 360_000;
export const SCALE_MIN = 0.0001;
export const SCALE_MAX = 10_000;

const vectorSchema = (minimum, maximum, { exclusive_minimum = false } = {}) => ({
  type: "array",
  minItems: 3,
  maxItems: 3,
  items: exclusive_minimum
    ? { type: "number", exclusiveMinimum: minimum, maximum }
    : { type: "number", minimum, maximum },
});

export const positionSchema = vectorSchema(-POSITION_LIMIT, POSITION_LIMIT);
export const rotationSchema = vectorSchema(-ROTATION_LIMIT, ROTATION_LIMIT);
export const scaleSchema = vectorSchema(SCALE_MIN, SCALE_MAX, {
  exclusive_minimum: true,
});
export const colorSchema = {
  type: "array",
  minItems: 4,
  maxItems: 4,
  items: { type: "number", minimum: 0, maximum: 1 },
};

export const transformInputSchema = {
  type: "object",
  properties: {
    position: positionSchema,
    rotation: rotationSchema,
    scale: scaleSchema,
  },
  additionalProperties: false,
};

export const transformPatchSchema = {
  ...transformInputSchema,
  minProperties: 1,
};

export const entityPropertiesSchema = {
  type: "object",
  properties: {
    color: colorSchema,
    visible: { type: "boolean" },
    label: { type: "string", maxLength: 256 },
  },
  additionalProperties: false,
};

export const entitySchema = {
  type: "object",
  required: [
    "entity_id",
    "generation",
    "name",
    "entity_type",
    "primitive_type",
    "transform",
    "properties",
  ],
  properties: {
    entity_id: { type: "string", minLength: 1, maxLength: 128 },
    generation: {
      type: "integer",
      minimum: 0,
      maximum: Number.MAX_SAFE_INTEGER,
    },
    name: { type: "string", minLength: 1, maxLength: 128 },
    entity_type: { const: "primitive" },
    primitive_type: {
      enum: ["cube", "sphere", "cylinder", "plane"],
    },
    transform: {
      type: "object",
      required: ["position", "rotation", "scale"],
      properties: {
        position: positionSchema,
        rotation: rotationSchema,
        scale: scaleSchema,
      },
      additionalProperties: false,
    },
    properties: {
      type: "object",
      required: ["color", "visible", "label"],
      properties: entityPropertiesSchema.properties,
      additionalProperties: false,
    },
  },
  additionalProperties: false,
};

export const worldSnapshotSchema = {
  type: "object",
  required: ["world_id", "revision", "entities", "history"],
  properties: {
    world_id: { type: "string", minLength: 1, maxLength: 128 },
    revision: {
      type: "integer",
      minimum: 0,
      maximum: Number.MAX_SAFE_INTEGER,
    },
    entities: {
      type: "array",
      items: entitySchema,
    },
    history: {
      type: "array",
    },
    adapter_metadata: {
      type: "object",
    },
  },
  additionalProperties: false,
};

export const envelopeSchema = {
  type: "object",
  required: [
    "protocol_version",
    "request_id",
    "session_id",
    "world_id",
    "type",
    "timestamp_ms",
    "payload",
  ],
  properties: {
    protocol_version: { const: PROTOCOL_VERSION },
    request_id: { type: "string", pattern: UUID_PATTERN },
    session_id: { type: "string", pattern: UUID_PATTERN },
    world_id: { type: "string", pattern: UUID_PATTERN },
    type: { type: "string", minLength: 1, maxLength: 128 },
    timestamp_ms: {
      type: "integer",
      minimum: 0,
      maximum: Number.MAX_SAFE_INTEGER,
    },
    payload: { type: "object" },
  },
  additionalProperties: false,
};

