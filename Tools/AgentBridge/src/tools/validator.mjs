import Ajv from "ajv";
import { AgentError } from "../protocol/errors.mjs";

export function createSchemaValidator() {
  return new Ajv({
    allErrors: true,
    strict: true,
    strictNumbers: true,
  });
}

function formatValidationErrors(errors = []) {
  return errors.map((error) => ({
    path: error.instancePath || "/",
    keyword: error.keyword,
    message: error.message || "invalid value",
    params: error.params,
  }));
}

export function assertValidArguments(tool, args) {
  const value = args ?? {};
  if (!tool.validate(value)) {
    throw new AgentError(
      "INVALID_ARGUMENT",
      `Invalid arguments for tool ${tool.name}`,
      {
        tool_name: tool.name,
        validation_errors: formatValidationErrors(tool.validate.errors),
      }
    );
  }
  assertFiniteNumbers(value);
  return value;
}

export function assertFiniteNumbers(value, path = "$") {
  if (typeof value === "number" && !Number.isFinite(value)) {
    throw new AgentError(
      "INVALID_ARGUMENT",
      `Non-finite number at ${path}`,
      { path }
    );
  }
  if (Array.isArray(value)) {
    value.forEach((entry, index) =>
      assertFiniteNumbers(entry, `${path}[${index}]`)
    );
  } else if (value && typeof value === "object") {
    for (const [key, entry] of Object.entries(value)) {
      assertFiniteNumbers(entry, `${path}.${key}`);
    }
  }
}

