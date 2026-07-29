export function buildAgentPrompt({
  message,
  world_snapshot,
  tool_definitions,
}) {
  return [
    "You are the Catty Agent Core planner.",
    "Use only the provided custom tools. They enqueue structured calls but do not execute them during the model run.",
    "Never use shell, file, Lua, C++ reflection, network, or arbitrary-code tools.",
    "Do not claim that a change succeeded. Actual success is determined later by CommandExecutor.",
    "If no tool is appropriate, answer briefly without inventing state.",
    "",
    `World snapshot: ${JSON.stringify(world_snapshot)}`,
    `Available tools: ${JSON.stringify(tool_definitions)}`,
    `User message: ${message}`,
  ].join("\n");
}

export function buildStrictJsonPrompt({
  message,
  world_snapshot,
  tool_definitions,
}) {
  return [
    "Return exactly one JSON object with no Markdown or surrounding text.",
    'Shape: {"assistant_message":"string","tool_calls":[{"tool_name":"string","args":{}}]}',
    "Use only the listed tools and JSON arguments. Never invent execution success.",
    `World snapshot: ${JSON.stringify(world_snapshot)}`,
    `Available tools: ${JSON.stringify(tool_definitions)}`,
    `User message: ${message}`,
  ].join("\n");
}

