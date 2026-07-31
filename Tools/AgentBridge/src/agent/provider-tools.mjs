import {
  ProviderError,
  providerErrorReasons,
} from "./providers/provider-errors.mjs";

const PROVIDER_TOOL_NAME = /^[A-Za-z0-9_-]{1,64}$/;

function normalizeProviderParameters(schema) {
  const parameters = structuredClone(schema);
  if (
    parameters &&
    typeof parameters === "object" &&
    !Array.isArray(parameters) &&
    parameters.type === undefined &&
    Array.isArray(parameters.oneOf)
  ) {
    parameters.type = "object";
    parameters.anyOf = parameters.oneOf;
    delete parameters.oneOf;
  }
  return parameters;
}

export class ToolNameMapper {
  constructor(tool_definitions) {
    if (!Array.isArray(tool_definitions)) {
      throw new TypeError("ToolNameMapper requires tool definitions");
    }
    this.internal_to_provider = new Map();
    this.provider_to_internal = new Map();
    for (const definition of tool_definitions) {
      const internal_name = definition?.name;
      if (typeof internal_name !== "string" || !internal_name) {
        throw new TypeError("Tool definition name is required");
      }
      const provider_name = internal_name.replaceAll(".", "__");
      if (!PROVIDER_TOOL_NAME.test(provider_name)) {
        throw new Error(
          `Tool name cannot be represented safely for a Provider: ${internal_name}`
        );
      }
      const existing = this.provider_to_internal.get(provider_name);
      if (existing && existing !== internal_name) {
        throw new Error(
          `Provider tool name collision: ${existing} and ${internal_name} both map to ${provider_name}`
        );
      }
      if (this.internal_to_provider.has(internal_name)) {
        throw new Error(`Duplicate internal tool name: ${internal_name}`);
      }
      this.internal_to_provider.set(internal_name, provider_name);
      this.provider_to_internal.set(provider_name, internal_name);
    }
  }

  toProviderName(internal_name) {
    const provider_name = this.internal_to_provider.get(internal_name);
    if (!provider_name) {
      throw new ProviderError(
        providerErrorReasons.UNKNOWN_TOOL,
        "Provider requested an unknown internal tool",
        { details: { provider_reason: providerErrorReasons.UNKNOWN_TOOL } }
      );
    }
    return provider_name;
  }

  toInternalName(provider_name) {
    const internal_name = this.provider_to_internal.get(provider_name);
    if (!internal_name) {
      throw new ProviderError(
        providerErrorReasons.UNKNOWN_TOOL,
        "Model returned an unknown Provider tool name",
        { details: { provider_reason: providerErrorReasons.UNKNOWN_TOOL } }
      );
    }
    return internal_name;
  }

  entries() {
    return [...this.internal_to_provider.entries()];
  }
}

export function toolDefinitionsToChatCompletions(
  tool_definitions,
  mapper = new ToolNameMapper(tool_definitions)
) {
  return tool_definitions.map((definition) => ({
    type: "function",
    function: {
      name: mapper.toProviderName(definition.name),
      description: definition.description,
      parameters: normalizeProviderParameters(definition.schema),
    },
  }));
}

export function normalizedToolCallsToChatCompletions(tool_calls, mapper) {
  return tool_calls.map((tool_call) => ({
    id: tool_call.tool_call_id,
    type: "function",
    function: {
      name: mapper.toProviderName(tool_call.tool_name),
      arguments: JSON.stringify(tool_call.args),
    },
  }));
}
