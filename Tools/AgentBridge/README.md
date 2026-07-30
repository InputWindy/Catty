# Catty AgentBridge

AgentBridge is a loopback-only Node.js service used by the existing Catty editor
Agent panel. It preserves the legacy chat API and also provides an independent
Agent Core v0.1 backed by an in-memory `MockWorld`.

Agent Core does **not** connect to the C++ game world in this version. It does
not expose shell commands, file tools, Lua execution, C++ reflection, pointers,
WebSockets, rendering, physics, or multiplayer features.

## Architecture

The v0.1 request path is:

```text
natural language
  -> AgentService
  -> MockProvider or CursorProvider
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

The existing `@cursor/sdk` dependency is retained. Version 1.0.26 exposes
`customTools`; `CursorProvider` uses those callbacks only to capture internal
ToolCalls and runs the SDK in plan mode. The callback does not modify
`MockWorld`. `CommandExecutor` remains the only authority for tool success.

Ajv is a direct dependency because tool contracts are published and compiled
as JSON Schema. Every v0.1 argument schema rejects additional properties.

## Requirements and install

- Node.js **22.13 or newer** (`@cursor/sdk@1.0.26` requires this)
- npm

```powershell
cd Tools\AgentBridge
npm install
npm test
```

All tests use `node:test`, random loopback ports, independent Sessions, and
MockProvider. No Cursor API key, network service, C++ game, or MyGame checkout
is required.

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

Without `CURSOR_API_KEY`, Mock mode is selected automatically. `--api-key` and
`--api-key-file` are retained for compatibility with the existing C++ client.
Their precedence is:

1. `--api-key`
2. `--api-key-file`
3. `CURSOR_API_KEY`

## Environment variables

| Variable | Default | Meaning |
| --- | --- | --- |
| `CATTY_AGENT_HOST` | `127.0.0.1` | Listen host; only `127.0.0.1` and `::1` are accepted |
| `CATTY_AGENT_PORT` | `8765` | Listen port; `--port` takes precedence |
| `CATTY_AGENT_MOCK` | automatic | `1` forces MockProvider |
| `CATTY_AGENT_DATA_DIR` | `Tools/AgentBridge/.runtime` | JSONL audit/runtime directory |
| `CURSOR_API_KEY` | empty | Cursor SDK key; absence selects Mock mode |

The legacy Cursor SDK JSONL store remains under
`<--cwd>/Saved/Agent/cursor-sdk-store` to avoid changing the existing editor
behavior. Agent Core audit data uses `CATTY_AGENT_DATA_DIR`.
`Tools/AgentBridge/.runtime/` is ignored by Git.

Request bodies are limited to 1 MiB by default. The server never listens on a
non-loopback address.

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

## MockWorld limits

- All state is in memory and disappears when the process exits.
- Each Session owns exactly one MockWorld.
- Entities are primitives only: `cube`, `sphere`, `cylinder`, or `plane`.
- Properties are restricted to `color`, `visible`, and `label`.
- Rotation uses three Euler-angle numbers.
- Undo is limited to the latest successful write transaction.
- The v0.1 UndoJournal stores a complete pre-transaction MockWorld snapshot.
  This is intentionally temporary and must be replaced by a real world adapter
  and engine-native change/undo mechanism when C++ integration is designed.
