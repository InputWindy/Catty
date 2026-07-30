import assert from "node:assert/strict";
import test from "node:test";
import { MockWorld } from "../src/world/mock-world.mjs";

test("MockWorld supports entity CRUD with allowlisted state", () => {
  const world = new MockWorld();
  const entity = world.spawnPrimitive({
    primitive_type: "cube",
    name: "TestCube",
    properties: { color: [1, 0, 0, 1] },
  });

  assert.equal(world.getEntity(entity.entity_id).name, "TestCube");
  assert.deepEqual(world.queryEntities({ primitive_type: "cube" }), [entity]);

  const transformed = world.setTransform(entity.entity_id, {
    position: [1, 2, 3],
  });
  assert.deepEqual(transformed.after.transform.position, [1, 2, 3]);

  const changed_property = world.setProperty(
    entity.entity_id,
    "visible",
    false
  );
  assert.equal(changed_property.after.properties.visible, false);

  assert.equal(world.destroyEntity(entity.entity_id).entity_id, entity.entity_id);
  assert.equal(world.entities.size, 0);
});

test("MockWorld state snapshots preserve and restore Map instances", () => {
  const world = new MockWorld();
  const entity = world.spawnPrimitive({ primitive_type: "sphere" });
  const state = world.captureState();

  assert.equal(state.entities instanceof Map, true);
  world.destroyEntity(entity.entity_id);
  world.restoreState(state);

  assert.equal(world.entities instanceof Map, true);
  assert.equal(world.getEntity(entity.entity_id).primitive_type, "sphere");
});

