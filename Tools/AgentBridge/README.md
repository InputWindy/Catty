# Catty AgentBridge

AgentBridge is a loopback-only Node.js service used by the existing Catty editor
Agent panel. It preserves the legacy chat API and provides Agent Core v0.3,
backed by an in-memory `MockWorld`.

The v0.3 theme is **Generic AI Provider Architecture and DeepSeek
Integration**. Real model planning is no longer coupled to Cursor SDK:
MockProvider, DeepSeek, generic OpenAI-compatible Chat Completions, and the
optional CursorProvider implement one internal Provider contract.

Agent Core does **not** connect to the C++ game world in this version. It does
not expose shell commands, file tools, Lua execution, C++ reflection, pointers,
WebSockets, rendering, physics, or multiplayer features.

## Architecture

The v0.3 request path is:

```text
natural language
  -> AgentService
  -> ProviderRegistry
       -> MockProvider
       -> OpenAICompatibleProvider -> DeepSeek preset
       -> CursorProvider
  -> structured ToolCall
  -> ToolRegistry + Ajv validation
  -> CommandExecutor
  -> MockWorld
  -> ChangeSet + UndoJournal
  -> HTTP response + JSONL audit log
```

`server.mjs` only loads configuration, creates the service objects, registers
the HTTP router, starts the server, and coordinates shutdown. Business logic is
under `src/`.

Provider code can only plan ToolCalls. It cannot modify MockWorld.
`CommandExecutor` remains the only execution path, and ToolResult remains the
only authority for success.

The existing `@cursor/sdk` dependency is retained. Version 1.0.26 exposes
`customTools`; `CursorProvider` uses those callbacks only to capture internal
ToolCalls and runs the SDK in plan mode. The callback does not modify
`MockWorld`. The SDK is dynamically imported only when Cursor is selected.

OpenAICompatibleProvider uses the Node.js 22 built-in `fetch` and standard Chat
Completions `/chat/completions`; no additional HTTP SDK is required. DeepSeek
is a preset over this implementation, not a separate network stack.

Ajv is a direct dependency because tool contracts are published and compiled
as JSON Schema. Every v1 tool argument schema rejects additional properties.

## Requirements and install

- Node.js **22.13 or newer** (`@cursor/sdk@1.0.26` requires this)
- npm

```powershell
cd Tools\AgentBridge
npm install
npm test
npm run eval
```

All default tests use `node:test`, random loopback ports, local fake HTTP
servers, independent Sessions, and MockWorld. No DeepSeek/Cursor API key,
external network service, C++ game, or MyGame checkout is required.

The install currently reports 3 dependency advisories (2 moderate and 1 high).
They require separate analysis; this version does not run
`npm audit fix --force` or upgrade unrelated dependencies.

## Provider selection

Selection order is deterministic:

1. `CATTY_AGENT_MOCK=1` forces MockProvider.
2. `CATTY_AI_PROVIDER` selects an explicit Provider.
3. Without an explicit Provider, legacy `CURSOR_API_KEY` selects Cursor.
4. Otherwise AgentBridge uses MockProvider.

`DEEPSEEK_API_KEY` alone never selects DeepSeek. Real Provider failures never
fall back to Mock, because fallback would hide configuration, authentication,
or model failures.

Supported Provider IDs:

- `mock` — deterministic and offline; the default.
- `deepseek` — the DeepSeek preset over OpenAICompatibleProvider.
- `openai-compatible` — a caller-configured Chat Completions endpoint.
- `cursor` — the retained optional Cursor SDK implementation.

See [AI Provider Architecture](docs/AI_PROVIDER_ARCHITECTURE.md) for the
contract and lifecycle, and [DeepSeek Setup](docs/DEEPSEEK_SETUP.md) for real
API configuration.

## Run

Default:

```powershell
cd Tools\AgentBridge
npm start
```

Legacy-compatible explicit command:

```powershell
node server.mjs --port 8765 --cwd C:\path\to\MyGame
```

Force deterministic Mock mode:

```powershell
$env:CATTY_AGENT_MOCK = "1"
npm start
```

Without an explicit Provider or `CURSOR_API_KEY`, Mock mode is selected
automatically. `--api-key` and `--api-key-file` are retained for compatibility
with the existing C++ client and select Cursor.
Their precedence is:

1. `--api-key`
2. `--api-key-file`
3. `CURSOR_API_KEY`

## CLI demo

The CLI creates its own in-memory Session, MockWorld, ToolRegistry,
CommandExecutor, AgentService, and selected Provider. It does not start the
HTTP server or occupy a port. With no Provider environment variables it remains
fully offline:

```powershell
cd Tools\AgentBridge
npm run demo
```

Enter natural-language commands directly. Built-in commands are:

- `/help`
- `/world` for a formatted snapshot
- `/entities` for a compact entity list
- `/undo` through the existing Agent/Undo path
- `/reset` for a new isolated Session
- `/exit`

Ctrl+C exits normally. Command errors are displayed without crashing the
process. Startup prints Provider, model, Mock/real mode, and
`Thinking: disabled`; it never prints a Key.

Select DeepSeek:

```powershell
$env:CATTY_AI_PROVIDER = "deepseek"
$env:DEEPSEEK_API_KEY = "replace_me"
npm run demo
```

Select Cursor:

```powershell
$env:CATTY_AI_PROVIDER = "cursor"
$env:CURSOR_API_KEY = "replace_me"
npm run demo
```

Select a generic OpenAI-compatible endpoint:

```powershell
$env:CATTY_AI_PROVIDER = "openai-compatible"
$env:CATTY_AI_BASE_URL = "https://provider.example/v1"
$env:CATTY_AI_MODEL = "provider-model"
$env:CATTY_AI_API_KEY = "replace_me"
npm run demo
```

Do not place real Key values in checked-in files or shell history.

## Behavior evaluations

Run all checked-in deterministic behavior cases with:

```powershell
npm run eval
```

Every scenario uses MockProvider with a fresh Session and MockWorld. No Cursor
API key, model service, or external network is used. JSON files under
`evals/cases/` define single- and multi-turn conversations separately from the
runner. The suite checks replies, tool names/counts, revision changes, undo
creation, final entity counts, transforms/properties, clarification, and
no-world-change behavior. A failure prints its scenario and turn with expected
and actual values and exits nonzero.

See [evals/README.md](evals/README.md) for the case format and current coverage.

## Session entity references

Each Session owns a small, in-memory reference context containing the last
created entity, last referenced entity, last query result IDs, and at most 20
recent entity IDs. New Sessions never inherit another Session's context.

Target resolution is deterministic:

1. an explicit `entity_id`;
2. a unique explicitly mentioned entity name;
3. a unique explicitly mentioned primitive type such as "the cube";
4. the last referenced entity that still exists;
5. the last created entity that still exists.

Deleted or undone-away entities are removed from the context. Every reference
is checked against the current MockWorld before use. Duplicate names or
primitive matches, missing pronoun targets, and other uncertain references
produce an assistant clarification with no ToolCall, revision change, or undo
token.

## MockProvider behavior

MockProvider remains deterministic, offline, and independent of the HTTP
router. It is a rule-based test provider, **not** a general natural-language
model.

Supported bounded expressions include:

- create `cube`, `sphere`, `cylinder`, and `plane`;
- red, green, blue, and white primitives;
- Chinese and English variants such as `生成一个红色方块`,
  `来个红色立方体`, `创建一个 red cube`, and `create a red cube`;
- list entities and query/delete by unique name or ID;
- references including `它`, `刚才那个`, `刚生成的`, and `it`;
- absolute position and scale;
- relative right/left movement on X and up/down movement on Z;
- double/half scale, color changes, hide/show, and latest undo;
- one-call compound spawns with initial position, scale, and/or color.

Requests for excessive entity counts, local files, JavaScript, PowerShell, or
system commands return no tools. Out-of-range coordinates and illegal scales
are rejected without bypassing the existing JSON Schemas.

## Environment variables

| Variable | Default | Meaning |
| --- | --- | --- |
| `CATTY_AGENT_HOST` | `127.0.0.1` | Listen host; only `127.0.0.1` and `::1` are accepted |
| `CATTY_AGENT_PORT` | `8765` | Listen port; `--port` takes precedence |
| `CATTY_AGENT_MOCK` | automatic | `1` forces MockProvider |
| `CATTY_AGENT_DATA_DIR` | `Tools/AgentBridge/.runtime` | JSONL audit/runtime directory |
| `CURSOR_API_KEY` | empty | Cursor SDK key; absence selects Mock mode |
| `CATTY_AI_PROVIDER` | selection rules above | `mock`, `deepseek`, `openai-compatible`, or `cursor` |
| `CATTY_AI_API_KEY` | empty | Generic Key; takes precedence over `DEEPSEEK_API_KEY` for DeepSeek |
| `CATTY_AI_BASE_URL` | Provider preset | Required for generic OpenAI-compatible |
| `CATTY_AI_MODEL` | Provider preset | Required for generic OpenAI-compatible |
| `CATTY_AI_TIMEOUT_MS` | `30000` | Per-model-request timeout |
| `CATTY_AI_MAX_RETRIES` | `1` | Retry count after the first model request |
| `CATTY_AI_TEMPERATURE` | `0` | Chat Completions temperature |
| `CATTY_AI_FINALIZE` | `true` | Enable one supported finalization request |
| `CATTY_AI_MAX_TOOL_CALLS` | `16` | Maximum planned ToolCalls |
| `DEEPSEEK_API_KEY` | empty | DeepSeek compatibility Key |

The legacy Cursor SDK JSONL store remains under
`<--cwd>/Saved/Agent/cursor-sdk-store` to avoid changing the existing editor
behavior. Agent Core audit data uses `CATTY_AGENT_DATA_DIR`.
`Tools/AgentBridge/.runtime/` is ignored by Git.

Request bodies are limited to 1 MiB by default. The server never listens on a
non-loopback address.

Generic OpenAI-compatible mode requires all of `CATTY_AI_BASE_URL`,
`CATTY_AI_MODEL`, and `CATTY_AI_API_KEY`. It receives no DeepSeek-specific
fields. DeepSeek defaults to `https://api.deepseek.com` and
`deepseek-v4-flash`, both overridable by generic variables. Agent Core v0.3
always disables thinking mode.

## Offline and real-network commands

These commands are offline and never make a real model request:

```powershell
npm test
npm run eval
```

These commands are explicit, optional real DeepSeek network operations and may
incur API charges:

```powershell
npm run smoke:deepseek
npm run eval:deepseek
```

Both real commands exit nonzero before any request when no DeepSeek Key is
configured. They are not part of `npm test`, `npm run eval`, or default CI.

## APIs

Legacy APIs, unchanged:

- `GET /health`
- `POST /chat`
- `GET /events?after=<id>`
- `POST /shutdown`

Agent Core v1 APIs:

- `GET /v1/health`
- `POST /v1/sessions`
- `POST /v1/agent/run`
- `POST /v1/tools/execute`
- `GET /v1/world/snapshot`
- `POST /v1/history/undo`
- `GET /v1/events`

The full request, response, revision, idempotency, batch, and undo contracts are
documented in [docs/AGENT_PROTOCOL_V1.md](docs/AGENT_PROTOCOL_V1.md).

## Quick Mock verification

Start the service in one PowerShell window:

```powershell
$env:CATTY_AGENT_MOCK = "1"
$env:CATTY_AGENT_PORT = "8765"
npm start
```

Use another PowerShell window:

```powershell
$base = "http://127.0.0.1:8765"
$session = Invoke-RestMethod -Method Post -Uri "$base/v1/sessions" `
  -ContentType "application/json" -Body "{}"

$requestId = [guid]::NewGuid().ToString()
$runBody = @{
  session_id = $session.session_id
  request_id = $requestId
  message = "生成一个红色立方体"
  expected_revision = 0
} | ConvertTo-Json
$run = Invoke-RestMethod -Method Post -Uri "$base/v1/agent/run" `
  -ContentType "application/json" -Body $runBody

Invoke-RestMethod -Method Get `
  -Uri "$base/v1/world/snapshot?session_id=$($session.session_id)"

$undoBody = @{
  session_id = $session.session_id
  request_id = [guid]::NewGuid().ToString()
  expected_revision = $run.world_revision
  undo_token = $run.undo_token
} | ConvertTo-Json
Invoke-RestMethod -Method Post -Uri "$base/v1/history/undo" `
  -ContentType "application/json" -Body $undoBody

Invoke-RestMethod -Method Post -Uri "$base/shutdown" `
  -ContentType "application/json" -Body "{}"
```

## MockWorld and current integration limits

- All state is in memory and disappears when the process exits.
- Each Session owns exactly one MockWorld.
- Entities are primitives only: `cube`, `sphere`, `cylinder`, or `plane`.
- Properties are restricted to `color`, `visible`, and `label`.
- Rotation uses three Euler-angle numbers.
- Undo is limited to the latest successful write transaction.
- The UndoJournal stores a complete pre-transaction MockWorld snapshot.
  This is intentionally temporary and must be replaced by a real world adapter
  and engine-native change/undo mechanism when C++ integration is designed.
- Agent Core v0.3 still has no C++ World adapter or game integration.
- Session state and reference context are not persisted.
- Provider finalization is limited to one text-only request; it cannot produce
  another executable ToolCall.
- Thinking mode, streaming, recursive agent loops, automatic Provider routing,
  and automatic fallback are not supported.
