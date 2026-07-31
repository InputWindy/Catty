import {
  UUID_PATTERN,
  entityPropertiesSchema,
  transformInputSchema,
  transformPatchSchema,
} from "../protocol/schemas.mjs";
import { ToolRegistry } from "./registry.mjs";

const emptyObjectSchema = {
  type: "object",
  properties: {},
  additionalProperties: false,
};

const entityIdSchema = {
  type: "object",
  required: ["entity_id"],
  properties: {
    entity_id: { type: "string", pattern: UUID_PATTERN },
  },
  additionalProperties: false,
};

export function createDefaultToolRegistry() {
  const registry = new ToolRegistry();

  registry.register({
    name: "world.get_summary",
    description: "Return the current world revision and entity counts.",
    schema: emptyObjectSchema,
    mutates_world: false,
    undoable: false,
  });

  registry.register({
    name: "world.query_entities",
    description:
      "Query or list world entities. Omit filters to list all entities, or filter by exact name, entity type, or primitive type.",
    schema: {
      type: "object",
      properties: {
        name: { type: "string", minLength: 1, maxLength: 128 },
        entity_type: { const: "primitive" },
        primitive_type: {
          enum: ["cube", "sphere", "cylinder", "plane"],
        },
        limit: { type: "integer", minimum: 1, maximum: 1000 },
      },
      additionalProperties: false,
    },
    mutates_world: false,
    undoable: false,
  });

  registry.register({
    name: "entity.get",
    description: "Return one entity by entity_id.",
    schema: entityIdSchema,
    mutates_world: false,
    undoable: false,
  });

  registry.register({
    name: "entity.spawn_primitive",
    description: "Spawn a bounded primitive entity in the current world.",
    schema: {
      type: "object",
      required: ["primitive_type"],
      properties: {
        primitive_type: {
          enum: ["cube", "sphere", "cylinder", "plane"],
        },
        name: { type: "string", minLength: 1, maxLength: 128 },
        transform: transformInputSchema,
        properties: entityPropertiesSchema,
      },
      additionalProperties: false,
    },
    mutates_world: true,
    undoable: true,
  });

  registry.register({
    name: "entity.set_transform",
    description: "Update position, Euler rotation, or scale for one entity.",
    schema: {
      type: "object",
      required: ["entity_id", "transform"],
      properties: {
        entity_id: { type: "string", pattern: UUID_PATTERN },
        transform: transformPatchSchema,
      },
      additionalProperties: false,
    },
    mutates_world: true,
    undoable: true,
  });

  registry.register({
    name: "entity.set_property",
    description: "Set one allowlisted entity property.",
    schema: {
      oneOf: [
        {
          type: "object",
          required: ["entity_id", "property_name", "value"],
          properties: {
            entity_id: { type: "string", pattern: UUID_PATTERN },
            property_name: { const: "color" },
            value: entityPropertiesSchema.properties.color,
          },
          additionalProperties: false,
        },
        {
          type: "object",
          required: ["entity_id", "property_name", "value"],
          properties: {
            entity_id: { type: "string", pattern: UUID_PATTERN },
            property_name: { const: "visible" },
            value: entityPropertiesSchema.properties.visible,
          },
          additionalProperties: false,
        },
        {
          type: "object",
          required: ["entity_id", "property_name", "value"],
          properties: {
            entity_id: { type: "string", pattern: UUID_PATTERN },
            property_name: { const: "label" },
            value: entityPropertiesSchema.properties.label,
          },
          additionalProperties: false,
        },
      ],
    },
    mutates_world: true,
    undoable: true,
  });

  registry.register({
    name: "entity.destroy",
    description: "Destroy one MockWorld entity by entity_id.",
    schema: entityIdSchema,
    mutates_world: true,
    undoable: true,
  });

  registry.register({
    name: "history.undo",
    description: "Undo the latest successful world write transaction.",
    schema: {
      type: "object",
      properties: {
        undo_token: { type: "string", pattern: UUID_PATTERN },
      },
      additionalProperties: false,
    },
    mutates_world: true,
    undoable: false,
  });

  return registry;
}
