# Agent Core v0.3 AI Provider Architecture

## Scope

Agent Core v0.3 separates model planning from world execution. It operates only
on the in-memory MockWorld and does not add an HTTP endpoint, game tool,
streaming transport, C++ adapter, persistence layer, or recursive agent loop.
Agent Protocol v1 remains unchanged.

```text
AgentService
├─ ProviderRegistry
│  ├─ MockProvider
│  ├─ OpenAICompatibleProvider
│  │  └─ DeepSeek preset
│  └─ CursorProvider
├─ ToolRegistry
├─ CommandExecutor
├─ SessionManager
└─ AuditLog
```

## Provider contract

Every production Provider exposes:

- `name` and `model`;
- boolean `capabilities`;
- `plan(input)`;
- optional `finalize(input)` when advertised;
- optional `close()`;
- `getMetadata()`.

Capabilities are:

```json
{
  "supports_tools": true,
  "supports_finalization": true,
  "supports_streaming": false,
  "supports_thinking": false
}
```

The plan input contains request and Session IDs, user text, normalized
messages, the current world snapshot, Session reference context, ToolRegistry
definitions, and an optional AbortSignal. The validated output is:

```json
{
  "provider": "deepseek",
  "model": "deepseek-v4-flash",
  "assistant_message": "",
  "tool_calls": [],
  "finish_reason": "stop",
  "usage": {
    "input_tokens": null,
    "output_tokens": null,
    "total_tokens": null,
    "cached_input_tokens": null
  },
  "provider_metadata": {}
}
```

Unknown usage values are `null`, never estimates. Runtime validation rejects
invalid output shapes, unsafe argument objects, sensitive metadata,
finalization ToolCalls, and excessive ToolCall counts.

Normalized messages are project-owned system, user, assistant, and tool
records. AgentService never reads OpenAI `choices`, serialized function
arguments, Cursor events, or Cursor streams.

## ProviderRegistry and lifecycle

ProviderRegistry receives normalized configuration, validates the Provider ID,
creates one selected Provider, reports safe metadata, and closes it during
shutdown. Provider-specific construction branches exist only in the Registry,
preset, or Provider implementation.

Selection order:

1. `CATTY_AGENT_MOCK=1` forces MockProvider.
2. Explicit `CATTY_AI_PROVIDER` wins.
3. Legacy `CURSOR_API_KEY` selects Cursor when no explicit Provider exists.
4. MockProvider is the default.

`DEEPSEEK_API_KEY` never selects DeepSeek by itself. A selected real Provider
that fails initialization or a request reports the real error; it never falls
back to Mock.

MockProvider is deterministic test scaffolding. DeepSeek is a preset over the
generic OpenAI-compatible implementation. CursorProvider remains a distinct
optional SDK integration and dynamically imports `@cursor/sdk` only when
selected.

## Tool name and Schema conversion

Internal tool names and Agent Protocol v1 keep their dotted names. The Provider
mapper replaces each dot with two underscores:

```text
entity.spawn_primitive -> entity__spawn_primitive
```

Mappings are stored bidirectionally, checked for collisions at construction,
and never reversed by an unchecked string replacement. An unknown Provider
name is rejected before CommandExecutor. Audit records prefer the internal
official name.

Chat Completions tools are generated directly from ToolRegistry definitions:

```json
{
  "type": "function",
  "function": {
    "name": "entity__spawn_primitive",
    "description": "...",
    "parameters": {}
  }
}
```

The JSON Schema is cloned from ToolRegistry, including
`additionalProperties: false`; there is no second Schema source. Provider
arguments are parsed only with `JSON.parse`, must be safe plain objects, and
are still validated by the existing Ajv path in CommandExecutor.

## Plan, execution, and finalization

The bounded flow is:

1. AgentService builds normalized messages and calls `plan()` once.
2. Zero ToolCalls returns Provider text without changing MockWorld.
3. Planned ToolCalls receive the current revision and are passed to
   CommandExecutor.
4. CommandExecutor performs registry lookup, Ajv validation, revision,
   idempotency, transaction, mutation, and Undo processing.
5. ToolResult is the only authority for success.
6. A Provider that supports and enables finalization may receive one
   text-only `finalize()` request containing the actual ToolResults.

Finalization receives no tools. A returned ToolCall is rejected. Finalization
never triggers another plan or tool execution. If finalization fails after
successful execution, the mutation remains committed and AgentService returns
a deterministic reply based on successful ToolResults. If a tool fails,
Provider text cannot override the failure reply.

## OpenAI-compatible requests

OpenAICompatibleProvider uses Node.js 22 `fetch` and sends
`POST <base_url>/chat/completions` with Bearer authorization, JSON content,
`stream: false`, model, normalized messages, tools for planning,
`tool_choice: auto`, and configured temperature. Trailing base URL slashes are
normalized.

The generic Provider requires explicit base URL, model, and generic API Key.
It receives no DeepSeek or Cursor fields. The DeepSeek preset adds only
centralized defaults and:

```json
{
  "thinking": {
    "type": "disabled"
  }
}
```

## Errors, cancellation, and retries

Internal ProviderError reasons distinguish configuration, unknown Provider,
missing Key, initialization, timeout, cancellation, connection failure, HTTP
status, invalid JSON/shape, invalid arguments, unknown tools, limits, and
invalid finalization. They map to the closest existing Agent Protocol v1 error
code with a safe `provider_reason` in details. No public v1 error code was
added.

Each model request has its own timeout and supports an upstream AbortSignal.
Provider shutdown aborts all in-flight requests. No ToolCall is executed after
planning times out or is cancelled.

Only connection failures, HTTP 408, 429, and 5xx are retryable. HTTP 400, 401,
403, response/Schema errors, ToolCall errors, and tool execution errors are
not retried. Retry count applies only to model requests. A finalization retry
can never re-execute tools.

## Audit safety

Provider phase audit records include Provider, model, plan/finalize phase,
attempt count, duration, HTTP status, finish reason, ToolCall count, token
usage, finalization state, timeout, and cancellation. Sanitization rejects Key,
Authorization, credential, and reasoning-content fields. Raw responses,
headers, hidden reasoning, and SDK credential objects are not logged.

## Adding a Provider

1. Implement the contract and safe metadata.
2. Convert normalized messages inside the Provider.
3. Generate tools from ToolRegistry and use ToolNameMapper.
4. Convert all failures to ProviderError.
5. Add one factory entry to ProviderRegistry.
6. Add offline tests for configuration, parsing, limits, cancellation,
   shutdown, errors, and Key redaction.
7. Keep world access out of the Provider; return ToolCalls only.
