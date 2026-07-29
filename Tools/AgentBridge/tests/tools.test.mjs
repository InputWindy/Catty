import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import test from "node:test";
import { AgentError } from "../src/protocol/errors.mjs";
import { createDefaultToolRegistry } from "../src/tools/definitions.mjs";

test("default registry exposes the eight v0.1 tools", () => {
  const registry = createDefaultToolRegistry();
  assert.deepEqual(
    registry.listDefinitions().map((definition) => definition.name),
    [
      "world.get_summary",
      "world.query_entities",
      "entity.get",
      "entity.spawn_primitive",
      "entity.set_transform",
      "entity.set_property",
      "entity.destroy",
      "history.undo",
    ]
  );
});

test("tool schemas reject unknown fields and unsafe numeric ranges", () => {
  const registry = createDefaultToolRegistry();

  assert.throws(
    () =>
      registry.validate("entity.spawn_primitive", {
        primitive_type: "cube",
        arbitrary: true,
      }),
    (error) =>
      error instanceof AgentError && error.code === "INVALID_ARGUMENT"
  );

  assert.throws(
    () =>
      registry.validate("entity.set_transform", {
        entity_id: randomUUID(),
        transform: { position: [Number.POSITIVE_INFINITY, 0, 0] },
      }),
    (error) =>
      error instanceof AgentError && error.code === "INVALID_ARGUMENT"
  );

  assert.throws(
    () =>
      registry.validate("entity.set_transform", {
        entity_id: randomUUID(),
        transform: { scale: [0, 1, 1] },
      }),
    (error) =>
      error instanceof AgentError && error.code === "INVALID_ARGUMENT"
  );
});

test("set_property accepts only color, visible, and label value types", () => {
  const registry = createDefaultToolRegistry();
  const entity_id = randomUUID();

  assert.deepEqual(
    registry.validate("entity.set_property", {
      entity_id,
      property_name: "color",
      value: [1, 0, 0, 1],
    }),
    {
      entity_id,
      property_name: "color",
      value: [1, 0, 0, 1],
    }
  );
  assert.throws(
    () =>
      registry.validate("entity.set_property", {
        entity_id,
        property_name: "script",
        value: "unsafe",
      }),
    (error) =>
      error instanceof AgentError && error.code === "INVALID_ARGUMENT"
  );
});

