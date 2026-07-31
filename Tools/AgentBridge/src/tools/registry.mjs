import { AgentError } from "../protocol/errors.mjs";
import {
  assertValidArguments,
  createSchemaValidator,
} from "./validator.mjs";

export class ToolRegistry {
  constructor({ validator = createSchemaValidator() } = {}) {
    this.validator = validator;
    this.tools = new Map();
  }

  register(definition) {
    const required_fields = [
      "name",
      "description",
      "schema",
      "mutates_world",
      "undoable",
    ];
    for (const field of required_fields) {
      if (definition[field] === undefined) {
        throw new Error(`Tool definition is missing ${field}`);
      }
    }
    if (this.tools.has(definition.name)) {
      throw new Error(`Tool already registered: ${definition.name}`);
    }
    const tool = {
      ...definition,
      validate: this.validator.compile(definition.schema),
    };
    this.tools.set(tool.name, tool);
    return tool;
  }

  get(name) {
    const tool = this.tools.get(name);
    if (!tool) {
      throw new AgentError("UNKNOWN_TOOL", `Unknown tool: ${name}`, {
        tool_name: name,
      });
    }
    return tool;
  }

  validate(name, args) {
    const tool = this.get(name);
    return assertValidArguments(tool, args);
  }

  listDefinitions() {
    return [...this.tools.values()].map((tool) => ({
      name: tool.name,
      description: tool.description,
      schema: structuredClone(tool.schema),
      mutates_world: tool.mutates_world,
      undoable: tool.undoable,
    }));
  }
}

