# Agent Core v0.4.1 World Adapter Architecture

## Scope

Agent Core v0.4.1 separates model planning and Agent Protocol handling from world
state and execution. Agent Protocol v1, the eight public game tools, Provider
contracts, and legacy HTTP endpoints remain unchanged.

Each Session owns exactly one `WorldAdapter`. The selected adapter is the only
authority for:

- world snapshots and entity DTOs;
- current and advanced revisions;
- ToolResults and ChangeSets;
- atomic execution and rollback;
- dry-run behavior;
- world-layer request idempotency;
- undo tokens and undo execution.

Agent Core does not synthesize a successful ToolResult, advance a revision,
restore a remote world, or create a remote undo record.

## Dependency direction

```text
AgentService
├─ ProviderRegistry -> selected Provider
├─ ToolRegistry -> eight schemas and metadata
├─ SessionManager
│  └─ WorldAdapterFactory
│     ├─ MockWorldAdapter -> MockWorld + UndoJournal
│     └─ RemoteWorldAdapter -> RemoteWorldClient -> HTTP/JSON world service
├─ CommandExecutor -> Session WorldAdapter only
└─ AuditLog
```

The Provider boundary and WorldAdapter boundary are independent. A Provider
receives a plain snapshot and may plan ToolCalls. It never imports MockWorld,
UndoJournal, RemoteWorldClient, or a concrete adapter. ToolRegistry remains the
single source of public tool names, descriptions, and argument schemas.

## WorldAdapter contract

Every adapter exposes:

```text
name
protocolVersion
capabilities
getSnapshot(input)
executeTransaction(input)
undo(input)
health(options)
getMetadata()
close()
```

The protocol version is `1.0`. Inputs and results are runtime validated and
must be plain JSON DTOs: objects, arrays, strings, booleans, `null`, and finite
numbers. Class instances, Map, Set, Buffer, functions, symbols, non-finite
numbers, excessive nesting, and prototype-pollution keys are rejected.
`AbortSignal` is an operational method option and is never serialized.

Capabilities advertise:

- `supports_atomic_transactions`;
- `supports_dry_run`;
- `supports_undo`;
- `supports_idempotency`;
- `max_tool_calls`;
- `supported_tools`.

All four booleans may honestly be false. `supported_tools` is a non-empty,
duplicate-free subset of ToolRegistry, and non-atomic adapters declare one
maximum ToolCall. Undo, dry-run, atomic batching, tool membership, and request
count dependencies are validated centrally. Capability mismatch is a hard
error; it does not change adapter selection.

The generic contract can express `supports_idempotency: false`, but the current
RemoteWorldAdapter rejects that profile during health negotiation. Remote
request replay is unsafe after an ambiguous transport outcome unless the world
service owns idempotent request records.

## Minimal World Profile

The testable minimal profile advertises only `world.get_summary`,
`entity.spawn_primitive`, and `entity.set_transform`; it has no atomic batches,
dry-run, or undo, and `max_tool_calls` is one. It is intended for the first Maho
C++ service and remains World Adapter Protocol v1. Full MockWorldAdapter and
full remote services continue to advertise all eight tools and all four
capabilities.

## Session ownership and lifecycle

`SessionManager.createSession()` asks `WorldAdapterFactory` for one adapter
using the new Session and world IDs. The Session stores only its adapter,
`world_id`, request replay state, concurrency state, last observed revision,
and entity reference context.

Session deletion closes and releases its adapter. AgentBridge shutdown closes
all Sessions, which aborts in-flight remote requests. The CLI `/reset` command
initializes the replacement adapter before replacing the active Session; a
failed replacement is closed and the previous Session remains valid.

Remote readiness is checked by `health()` and an initial authoritative
snapshot. Health results are cached for one second and invalidated on close.

## Execution path

```text
Agent Protocol ToolCalls
  -> envelope and UUID validation
  -> ToolRegistry name/schema validation
  -> centralized adapter capability validation
  -> one normalized adapter request
  -> adapter-owned transaction
  -> runtime validation of correlation, revisions, and ToolResults
  -> unchanged Agent Protocol v1 response
```

`CommandExecutor` retains Agent-facing validation and Session-level replay
coalescing. It does not inspect or mutate world storage. All calls in a batch
must have one expected revision and one dry-run value. `history.undo` must be a
single-call transaction and is routed to `adapter.undo()`.

Before any adapter method call, CommandExecutor rejects unsupported tool names,
requests over `max_tool_calls`, non-atomic batches, unsupported dry-run, and
unsupported undo. A full adapter receives `atomic: true`; a minimal
single-operation request receives `atomic: false`. A rejected batch is never
split, and revision, undo token, world state, and EntityContext remain
unchanged.

After the initial remote health/snapshot negotiation, AgentService filters the
ToolRegistry definition list by the Session adapter's `supported_tools`. The
filter preserves registry order and reuses the authoritative definitions and
schemas. It applies identically to MockProvider and real Providers. Provider
output remains untrusted: CommandExecutor repeats capability validation even
if a Provider returns a tool that was not exposed.

On a successful response, CommandExecutor requests an authoritative snapshot
before updating `EntityContext`. A missing, malformed, cancelled, or
uncorrelated snapshot leaves EntityContext unchanged. A validated remote
execution result remains authoritative even when this follow-up refresh fails.

## MockWorldAdapter

MockWorldAdapter wraps the v0.3 in-memory implementation without changing its
public behavior. It privately owns one MockWorld, one UndoJournal, execution
handlers, request replay records, revision advancement, dry-run restoration,
atomic rollback, ChangeSet construction, and undo.

Mock is the default. It is deterministic, offline, and suitable for existing
tests and local development.

## RemoteWorldAdapter

RemoteWorldAdapter uses the
[Maho World Adapter Protocol v1](MAHO_WORLD_ADAPTER_PROTOCOL_V1.md). It:

- validates health, version, capabilities, every response DTO, and correlation;
- stores and reports the remote service's negotiated capability subset;
- enforces the same capability request checks before HTTP execute or undo;
- sends the auth token only in `Authorization: Bearer ...`;
- applies one configured timeout to each request;
- distinguishes timeout, external cancellation, shutdown cancellation,
  connection failure, invalid JSON, HTTP failure, and invalid response shape;
- limits response bodies to 4 MiB;
- performs no automatic retry;
- performs no local world rollback;
- retains only the latest opaque undo token;
- never falls back to MockWorldAdapter.

Remote base URLs must be credential-free HTTP(S) URLs without query strings or
fragments. Loopback is the default security boundary. Non-loopback access
requires both explicit opt-in and a bearer token.

## Selection and configuration

World selection is independent from `MAHO_AI_PROVIDER`:

| Variable | Default | Purpose |
| --- | --- | --- |
| `MAHO_WORLD_ADAPTER` | `mock` | `mock` or explicit `remote` |
| `MAHO_WORLD_BASE_URL` | `http://127.0.0.1:8770` | Remote base URL |
| `MAHO_WORLD_TIMEOUT_MS` | `5000` | Request timeout, 1-300000 ms |
| `MAHO_WORLD_AUTH_TOKEN` | empty | Bearer token |
| `MAHO_WORLD_ALLOW_NON_LOOPBACK` | `0` | Explicit non-loopback opt-in |

Unknown adapter IDs and invalid configurations fail startup. A remote failure
never changes the configured adapter ID.

## Errors and Agent Protocol compatibility

WorldAdapter failures use internal reasons and safe detail fields. At the
AgentBridge boundary they map to existing Agent Protocol v1 codes:

| Adapter reason | Agent Protocol code |
| --- | --- |
| configuration, URL, loopback, or token validation | `INVALID_REQUEST` |
| timeout | `TIMEOUT` |
| revision conflict or HTTP 409 | `REVISION_CONFLICT` |
| missing entity | `ENTITY_NOT_FOUND` |
| unknown tool | `UNKNOWN_TOOL` |
| invalid argument | `INVALID_ARGUMENT` |
| insufficient capability | `INVALID_REQUEST` |
| unavailable undo | `UNDO_NOT_AVAILABLE` |
| HTTP 401/403 | `PERMISSION_DENIED` |
| transport, cancellation, protocol, shape, or correlation failure | `EXECUTION_FAILED` |

The public error envelope is unchanged. Safe `details` may include adapter,
phase, status, timeout/cancellation flags, correlation field, revision, and
tool identifiers. It never includes headers, raw bodies, credentials, or
arbitrary remote exception objects.

## Audit and observability

Audit records preserve the existing fields and add only safe adapter metadata:
adapter name and protocol version, operation phase, duration, HTTP status,
replay flag, timeout/cancellation flags, normalized remote error class,
supported tool name array, maximum call count, atomic/dry-run/undo booleans,
and a normalized capability rejection reason.
Existing recursive sensitive-key redaction remains active. Startup and CLI
output may show the credential-free base URL but never the token.

## Verification

The shared conformance test runs the same assertions against MockWorldAdapter
and RemoteWorldAdapter backed by an isolated fake HTTP server. Default tests
remain offline.

```powershell
npm test
npm run eval
npm run eval:remote
npm run eval:remote:minimal
npm run smoke:remote
```

`eval:remote` reuses every default behavior JSON case. `smoke:remote` verifies
health, snapshot, spawn, transform, undo, and final authoritative state.
`eval:remote:minimal` reuses the same runner with five supported minimal cases;
it does not require the full 26-case behavior suite to pass under a three-tool
profile. Golden JSON fixtures under `tests/fixtures/world-adapter-v1/` are
validated by the runtime contracts and are available to future C++ tests.

## Current limits

- No production C++ or game-world adapter is included.
- FakeMahoWorldServer is test and local-development scaffolding.
- State is not persisted by the included mock/fake implementations.
- Full-profile undo remains limited to the latest successful write transaction;
  the Minimal World Profile has no undo.
- There is no streaming, WebSocket transport, distributed transaction,
  recursive agent loop, or automatic adapter routing.

## Adding another adapter

An additional adapter must implement and pass the existing runtime contract,
reuse ToolRegistry names and schemas, return the same snapshot/entity and
ToolResult semantics, add an explicit factory ID and configuration path, and
run the shared conformance assertions. It must not be selected automatically
or introduce branches in AgentService, CommandExecutor, Router, CLI, or
Provider code. Any new transport needs equivalent URL, credential, response
size, cancellation, correlation, lifecycle, and audit-safety tests.

## Future C++ integration

A production Maho adapter remains future work. The game process must remain
the sole world authority and expose only DTO snapshots and command results. An
HTTP handler must enqueue validated transactions into a bounded, thread-safe
main-thread command queue. World lookup, entity validation, transaction
execution, ChangeSet construction, revision advancement, and engine-native
undo must occur in a safe main-thread phase; an HTTP worker must never mutate
the World, dereference engine objects, or send pointers/object addresses.

The engine implementation also needs lifecycle ownership, queue backpressure,
request-id persistence for the desired replay window, atomic command staging,
main-thread timeout/cancellation semantics, and conformance tests against the
same protocol behavior.
