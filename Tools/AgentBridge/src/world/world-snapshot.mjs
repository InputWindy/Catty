export function cloneValue(value) {
  return structuredClone(value);
}

export function entityToJson(entity) {
  return cloneValue(entity);
}

export function worldToSnapshot(world) {
  return {
    world_id: world.world_id,
    revision: world.revision,
    entities: [...world.entities.values()].map(entityToJson),
    history: cloneValue(world.history),
  };
}

