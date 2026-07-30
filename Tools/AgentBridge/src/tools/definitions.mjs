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
    description: "Return the current MockWorld revision and entity counts.",
    schema: emptyObjectSchema,
    mutates_world: false,
    undoable: false,
    execute: ({ world }) => {
      const by_primitive_type = {};
      for (const entity of world.entities.values()) {
        by_primitive_type[entity.primitive_type] =
          (by_primitive_type[entity.primitive_type] || 0) + 1;
      }
      return {
        data: {
          world_id: world.world_id,
          revision: world.revision,
          entity_count: world.entities.size,
          by_primitive_type,
        },
        changes: [],
      };
    },
  });

  registry.register({
    name: "world.query_entities",
    description:
      "Query MockWorld entities by exact name, entity type, or primitive type.",
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
    execute: ({ world, args }) => ({
      data: {
        entities: world.queryEntities(args),
      },
      changes: [],
    }),
  });

  registry.register({
    name: "entity.get",
    description: "Return one entity by entity_id.",
    schema: entityIdSchema,
    mutates_world: false,
    undoable: false,
    execute: ({ world, args }) => ({
      data: {
        entity: world.getEntity(args.entity_id),
      },
      changes: [],
    }),
  });

  registry.register({
    name: "entity.spawn_primitive",
    description: "Spawn a bounded MockWorld primitive entity.",
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
    execute: ({ world, args }) => {
      const entity = world.spawnPrimitive(args);
      return {
        data: { entity },
        changes: [
          {
            operation: "spawn",
            entity_id: entity.entity_id,
            before: null,
            after: entity,
          },
        ],
      };
    },
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
    execute: ({ world, args }) => {
      const changed = world.setTransform(args.entity_id, args.transform);
      return {
        data: { entity: changed.after },
        changes: [
          {
            operation: "set_transform",
            entity_id: args.entity_id,
            before: changed.before,
            after: changed.after,
          },
        ],
      };
    },
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
    execute: ({ world, args }) => {
      const changed = world.setProperty(
        args.entity_id,
        args.property_name,
        args.value
      );
      return {
        data: { entity: changed.after },
        changes: [
          {
            operation: "set_property",
            entity_id: args.entity_id,
            property_name: args.property_name,
            before: changed.before,
            after: changed.after,
          },
        ],
      };
    },
  });

  registry.register({
    name: "entity.destroy",
    description: "Destroy one MockWorld entity by entity_id.",
    schema: entityIdSchema,
    mutates_world: true,
    undoable: true,
    execute: ({ world, args }) => {
      const entity = world.destroyEntity(args.entity_id);
      return {
        data: { entity_id: args.entity_id },
        changes: [
          {
            operation: "destroy",
            entity_id: args.entity_id,
            before: entity,
            after: null,
          },
        ],
      };
    },
  });

  registry.register({
    name: "history.undo",
    description: "Undo the latest successful MockWorld write transaction.",
    schema: {
      type: "object",
      properties: {
        undo_token: { type: "string", pattern: UUID_PATTERN },
      },
      additionalProperties: false,
    },
    mutates_world: true,
    undoable: false,
    execute: ({ world, args, undo_journal }) =>
      undo_journal.undo(world, args.undo_token),
  });

  return registry;
}

