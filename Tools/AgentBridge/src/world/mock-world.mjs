import { randomUUID } from "node:crypto";
import { AgentError } from "../protocol/errors.mjs";
import { cloneValue, worldToSnapshot } from "./world-snapshot.mjs";

const defaultTransform = () => ({
  position: [0, 0, 0],
  rotation: [0, 0, 0],
  scale: [1, 1, 1],
});

const defaultProperties = () => ({
  color: [1, 1, 1, 1],
  visible: true,
  label: "",
});

export class MockWorld {
  constructor({ world_id = randomUUID() } = {}) {
    this.world_id = world_id;
    this.revision = 0;
    this.entities = new Map();
    this.history = [];
  }

  snapshot() {
    return worldToSnapshot(this);
  }

  captureState() {
    const state = structuredClone({
      world_id: this.world_id,
      revision: this.revision,
      entities: this.entities,
      history: this.history,
    });
    if (!(state.entities instanceof Map)) {
      throw new AgentError(
        "INTERNAL_ERROR",
        "MockWorld structuredClone did not preserve Map"
      );
    }
    return state;
  }

  restoreState(state, { revision = state.revision } = {}) {
    const restored = structuredClone(state);
    if (!(restored.entities instanceof Map)) {
      throw new AgentError(
        "INTERNAL_ERROR",
        "MockWorld snapshot does not contain an entity Map"
      );
    }
    this.world_id = restored.world_id;
    this.revision = revision;
    this.entities = restored.entities;
    this.history = restored.history;
  }

  getEntity(entity_id) {
    const entity = this.entities.get(entity_id);
    if (!entity) {
      throw new AgentError(
        "ENTITY_NOT_FOUND",
        `Entity not found: ${entity_id}`,
        { entity_id }
      );
    }
    return cloneValue(entity);
  }

  queryEntities({ name, entity_type, primitive_type, limit = 100 } = {}) {
    const results = [];
    for (const entity of this.entities.values()) {
      if (name !== undefined && entity.name !== name) {
        continue;
      }
      if (entity_type !== undefined && entity.entity_type !== entity_type) {
        continue;
      }
      if (
        primitive_type !== undefined &&
        entity.primitive_type !== primitive_type
      ) {
        continue;
      }
      results.push(cloneValue(entity));
      if (results.length >= limit) {
        break;
      }
    }
    return results;
  }

  spawnPrimitive({
    primitive_type,
    name,
    transform = {},
    properties = {},
  }) {
    const entity = {
      entity_id: randomUUID(),
      generation: 1,
      name: name || `${primitive_type}_${this.entities.size + 1}`,
      entity_type: "primitive",
      primitive_type,
      transform: {
        ...defaultTransform(),
        ...cloneValue(transform),
      },
      properties: {
        ...defaultProperties(),
        ...cloneValue(properties),
      },
    };
    this.entities.set(entity.entity_id, entity);
    return cloneValue(entity);
  }

  setTransform(entity_id, transform) {
    const entity = this.entities.get(entity_id);
    if (!entity) {
      throw new AgentError(
        "ENTITY_NOT_FOUND",
        `Entity not found: ${entity_id}`,
        { entity_id }
      );
    }
    const before = cloneValue(entity);
    entity.transform = {
      ...entity.transform,
      ...cloneValue(transform),
    };
    return {
      before,
      after: cloneValue(entity),
    };
  }

  setProperty(entity_id, property_name, value) {
    const entity = this.entities.get(entity_id);
    if (!entity) {
      throw new AgentError(
        "ENTITY_NOT_FOUND",
        `Entity not found: ${entity_id}`,
        { entity_id }
      );
    }
    const before = cloneValue(entity);
    entity.properties[property_name] = cloneValue(value);
    return {
      before,
      after: cloneValue(entity),
    };
  }

  destroyEntity(entity_id) {
    const entity = this.entities.get(entity_id);
    if (!entity) {
      throw new AgentError(
        "ENTITY_NOT_FOUND",
        `Entity not found: ${entity_id}`,
        { entity_id }
      );
    }
    this.entities.delete(entity_id);
    return cloneValue(entity);
  }
}

