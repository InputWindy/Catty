export function buildAgentPrompt({
  message,
  world_snapshot,
  tool_definitions,
}) {
  return [
    "You are the Maho Agent Core planner.",
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

export function buildProviderSystemPrompt({
  world_snapshot,
  tool_definitions,
  session_context,
}) {
  return [
    "You are the Maho Agent Core planner for an in-memory MockWorld.",
    "Use only the provided tools. Never use shell, files, Lua, JavaScript, PowerShell, network tools, or arbitrary code execution.",
    "Tool calls are proposals. CommandExecutor performs validation and is the only authority for success or failure.",
    "Never claim a world change succeeded before receiving its ToolResult.",
    "If the target or intent is ambiguous, ask a short clarifying question and return no ToolCall.",
    "The current world snapshot is authoritative for this single planning phase. If an exact name uniquely identifies an entity there, use its entity_id directly for get, update, or destroy; do not issue a preliminary query.",
    "For requests to list entities, use world.query_entities with no filters. world.get_summary returns counts only and does not list entities.",
    "Do not reveal hidden reasoning or chain-of-thought.",
    `Current world snapshot: ${JSON.stringify(world_snapshot)}`,
    `Current Session context: ${JSON.stringify(session_context)}`,
    `Available tools: ${JSON.stringify(tool_definitions)}`,
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
