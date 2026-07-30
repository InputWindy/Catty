# DeepSeek setup for Agent Core v0.3

DeepSeek is an explicit real-network Provider for Agent Core. It uses the
generic OpenAI-compatible Chat Completions implementation and still operates
only on the in-memory MockWorld. It does not connect to the C++ game World.

## Configuration

Required:

- `CATTY_AI_PROVIDER=deepseek`
- either `CATTY_AI_API_KEY` or `DEEPSEEK_API_KEY`

`CATTY_AI_API_KEY` takes precedence when both Key variables are set.
`DEEPSEEK_API_KEY` alone does not select the Provider.

Defaults:

- base URL: `https://api.deepseek.com`
- model: `deepseek-v4-flash`
- timeout: 30000 ms
- model retries: 1
- maximum ToolCalls: 16
- thinking: disabled

Override the model with `CATTY_AI_MODEL` and the endpoint with
`CATTY_AI_BASE_URL`.

## PowerShell

Set secrets only in the current process environment:

```powershell
cd Tools\AgentBridge
$env:CATTY_AI_PROVIDER = "deepseek"
$env:DEEPSEEK_API_KEY = "replace_me"
$env:CATTY_AI_MODEL = "deepseek-v4-flash"
npm run demo
```

Optional real checks:

```powershell
npm run smoke:deepseek
npm run eval:deepseek
```

Remove the Key from the current PowerShell process when finished:

```powershell
Remove-Item Env:DEEPSEEK_API_KEY
```

## POSIX shell

```sh
cd Tools/AgentBridge
export CATTY_AI_PROVIDER=deepseek
export DEEPSEEK_API_KEY=replace_me
export CATTY_AI_MODEL=deepseek-v4-flash
npm run demo
```

Optional real checks:

```sh
npm run smoke:deepseek
npm run eval:deepseek
```

Remove the Key when finished:

```sh
unset DEEPSEEK_API_KEY
```

## Commands and network behavior

- `npm test` is offline and uses local fake HTTP servers.
- `npm run eval` is offline and evaluates MockProvider.
- `npm run demo` uses the selected Provider; it is offline by default and
  accesses DeepSeek only when explicitly selected.
- `npm run smoke:deepseek` makes a small number of real requests.
- `npm run eval:deepseek` runs 10 real-provider behavior cases.

The real smoke and evaluation commands exit nonzero before any network request
when no Key is configured. They are never run by default tests or CI.

Real DeepSeek API requests may incur fees and can vary with service/model
availability. The real evaluation checks ToolCalls, arguments, ToolResults,
and final MockWorld state rather than exact response wording.

## Non-thinking limitation

Agent Core v0.3 does not support DeepSeek thinking mode. Every DeepSeek request
explicitly sends `thinking.type=disabled`, the Provider advertises
`supports_thinking=false`, and attempts to enable thinking are configuration
errors. Reasoning content and chain-of-thought are not processed or logged.

## Key safety

- Keep Keys only in process environment variables.
- Never put a real Key in `.env.example`, README files, source, tests, logs,
  snapshots, command transcripts, or Git.
- `.env` and `.env.local` are ignored; `.env.example` contains placeholders
  only. AgentBridge does not load dotenv files.
- AgentBridge never logs Authorization headers, complete HTTP headers, or raw
  Provider responses.
- A real Provider failure is reported directly and never falls back to Mock.
