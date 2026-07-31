import assert from "node:assert/strict";
import test from "node:test";
import {
  ToolNameMapper,
  toolDefinitionsToChatCompletions,
} from "../src/agent/provider-tools.mjs";
import { ProviderError } from "../src/agent/providers/provider-errors.mjs";
import { createDefaultToolRegistry } from "../src/tools/definitions.mjs";

test("all eight ToolRegistry names map stably and reversibly", () => {
  const definitions = createDefaultToolRegistry().listDefinitions();
  const mapper = new ToolNameMapper(definitions);
  assert.equal(mapper.entries().length, 8);
  assert.equal(
    mapper.toProviderName("entity.spawn_primitive"),
    "entity__spawn_primitive"
  );
  for (const definition of definitions) {
    const provider_name = mapper.toProviderName(definition.name);
    assert.equal(mapper.toInternalName(provider_name), definition.name);
  }
});

test("tool name mapping rejects startup collisions and unknown Provider names", () => {
  assert.throws(
    () =>
      new ToolNameMapper([
        { name: "entity.spawn" },
        { name: "entity__spawn" },
      ]),
    /collision/
  );
  const mapper = new ToolNameMapper([{ name: "world.get_summary" }]);
  assert.throws(
    () => mapper.toInternalName("shell__execute"),
    (error) =>
      error instanceof ProviderError && error.reason === "unknown_tool"
  );
});

test("Chat Completions tools come from ToolRegistry schemas without weakening them", () => {
  const definitions = createDefaultToolRegistry().listDefinitions();
  const mapper = new ToolNameMapper(definitions);
  const tools = toolDefinitionsToChatCompletions(definitions, mapper);
  assert.equal(tools.length, 8);
  assert.deepEqual(
    tools.map((tool) => tool.function.name),
    mapper.entries().map(([, provider_name]) => provider_name)
  );
  const spawn_definition = definitions.find(
    (definition) => definition.name === "entity.spawn_primitive"
  );
  const spawn_tool = tools.find(
    (tool) => tool.function.name === "entity__spawn_primitive"
  );
  assert.deepEqual(spawn_tool.function.parameters, spawn_definition.schema);
  assert.equal(spawn_tool.function.parameters.additionalProperties, false);
  assert.notEqual(spawn_tool.function.parameters, spawn_definition.schema);

  const property_definition = definitions.find(
    (definition) => definition.name === "entity.set_property"
  );
  const property_tool = tools.find(
    (tool) => tool.function.name === "entity__set_property"
  );
  assert.equal(property_tool.function.parameters.type, "object");
  assert.equal(property_tool.function.parameters.oneOf, undefined);
  assert.deepEqual(
    property_tool.function.parameters.anyOf,
    property_definition.schema.oneOf
  );
  assert.ok(
    property_tool.function.parameters.anyOf.every(
      (branch) => branch.additionalProperties === false
    )
  );
  assert.ok(Array.isArray(property_definition.schema.oneOf));
  assert.equal(property_definition.schema.anyOf, undefined);
});
