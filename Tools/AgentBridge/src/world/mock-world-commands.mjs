export function createMockWorldCommandHandlers() {
  return new Map([
    [
      "world.get_summary",
      ({ world }) => {
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
    ],
    [
      "world.query_entities",
      ({ world, args }) => ({
        data: {
          entities: world.queryEntities(args),
        },
        changes: [],
      }),
    ],
    [
      "entity.get",
      ({ world, args }) => ({
        data: {
          entity: world.getEntity(args.entity_id),
        },
        changes: [],
      }),
    ],
    [
      "entity.spawn_primitive",
      ({ world, args }) => {
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
    ],
    [
      "entity.set_transform",
      ({ world, args }) => {
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
    ],
    [
      "entity.set_property",
      ({ world, args }) => {
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
    ],
    [
      "entity.destroy",
      ({ world, args }) => {
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
    ],
  ]);
}
