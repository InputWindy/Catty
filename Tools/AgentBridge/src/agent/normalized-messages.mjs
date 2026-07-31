import { AgentError } from "../protocol/errors.mjs";

const MESSAGE_ROLES = new Set(["system", "user", "assistant", "tool"]);
const POLLUTION_KEYS = new Set(["__proto__", "prototype", "constructor"]);

export function isPlainObject(value) {
  if (!value || typeof value !== "object" || Array.isArray(value)) {
    return false;
  }
  const prototype = Object.getPrototypeOf(value);
  return prototype === Object.prototype || prototype === null;
}

export function assertSafePlainObject(value, label = "value") {
  if (!isPlainObject(value)) {
    throw new AgentError(
      "MODEL_OUTPUT_INVALID",
      `${label} must be a plain object`,
      { field: label }
    );
  }
  for (const [key, entry] of Object.entries(value)) {
    if (POLLUTION_KEYS.has(key)) {
      throw new AgentError(
        "MODEL_OUTPUT_INVALID",
        `${label} contains a forbidden key`,
        { field: label, key }
      );
    }
    if (Array.isArray(entry)) {
      entry.forEach((item, index) => {
        if (item && typeof item === "object") {
          assertSafePlainObject(item, `${label}.${key}[${index}]`);
        }
      });
    } else if (entry && typeof entry === "object") {
      assertSafePlainObject(entry, `${label}.${key}`);
    }
  }
  return value;
}

function assertTextContent(message, index) {
  if (typeof message.content !== "string") {
    throw new AgentError(
      "MODEL_OUTPUT_INVALID",
      `normalized_messages[${index}].content must be a string`,
      { message_index: index }
    );
  }
}

function assertNormalizedToolCall(tool_call, message_index, call_index) {
  if (!isPlainObject(tool_call)) {
    throw new AgentError(
      "MODEL_OUTPUT_INVALID",
      "Normalized assistant tool_calls must contain objects",
      { message_index, tool_call_index: call_index }
    );
  }
  if (
    typeof tool_call.tool_call_id !== "string" ||
    !tool_call.tool_call_id ||
    typeof tool_call.tool_name !== "string" ||
    !tool_call.tool_name
  ) {
    throw new AgentError(
      "MODEL_OUTPUT_INVALID",
      "Normalized assistant ToolCall is missing an id or name",
      { message_index, tool_call_index: call_index }
    );
  }
  assertSafePlainObject(
    tool_call.args,
    `normalized_messages[${message_index}].tool_calls[${call_index}].args`
  );
}

/**
 * @typedef {object} NormalizedSystemMessage
 * @property {"system"} role
 * @property {string} content
 *
 * @typedef {object} NormalizedUserMessage
 * @property {"user"} role
 * @property {string} content
 *
 * @typedef {object} NormalizedAssistantMessage
 * @property {"assistant"} role
 * @property {string} content
 * @property {Array<{tool_call_id: string, tool_name: string, args: object}>} tool_calls
 *
 * @typedef {object} NormalizedToolMessage
 * @property {"tool"} role
 * @property {string} tool_call_id
 * @property {string} name
 * @property {string} content
 *
 * @typedef {NormalizedSystemMessage|NormalizedUserMessage|NormalizedAssistantMessage|NormalizedToolMessage} NormalizedMessage
 */

export function assertNormalizedMessages(messages) {
  if (!Array.isArray(messages)) {
    throw new AgentError(
      "MODEL_OUTPUT_INVALID",
      "normalized_messages must be an array"
    );
  }
  for (const [index, message] of messages.entries()) {
    if (!isPlainObject(message) || !MESSAGE_ROLES.has(message.role)) {
      throw new AgentError(
        "MODEL_OUTPUT_INVALID",
        "Normalized message has an invalid role or shape",
        { message_index: index }
      );
    }
    assertTextContent(message, index);
    if (message.role === "assistant") {
      if (!Array.isArray(message.tool_calls)) {
        throw new AgentError(
          "MODEL_OUTPUT_INVALID",
          "Normalized assistant message requires tool_calls",
          { message_index: index }
        );
      }
      message.tool_calls.forEach((tool_call, call_index) =>
        assertNormalizedToolCall(tool_call, index, call_index)
      );
    }
    if (message.role === "tool") {
      if (
        typeof message.tool_call_id !== "string" ||
        !message.tool_call_id ||
        typeof message.name !== "string" ||
        !message.name
      ) {
        throw new AgentError(
          "MODEL_OUTPUT_INVALID",
          "Normalized tool message requires tool_call_id and name",
          { message_index: index }
        );
      }
    }
  }
  return messages;
}

export function createPlanMessages({
  system_message,
  session_messages = [],
  user_message,
}) {
  const messages = [
    { role: "system", content: String(system_message) },
    ...structuredClone(session_messages),
    { role: "user", content: String(user_message) },
  ];
  return assertNormalizedMessages(messages);
}

export function createAssistantToolMessage(provider_output) {
  return {
    role: "assistant",
    content: provider_output.assistant_message,
    tool_calls: structuredClone(provider_output.tool_calls),
  };
}

export function createToolResultMessage(tool_call, tool_result) {
  return {
    role: "tool",
    tool_call_id: tool_call.tool_call_id,
    name: tool_call.tool_name,
    content: JSON.stringify(tool_result),
  };
}
