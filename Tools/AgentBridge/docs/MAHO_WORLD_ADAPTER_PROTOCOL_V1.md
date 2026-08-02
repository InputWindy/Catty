# Maho World Adapter Protocol v1

## Status and scope

This document defines the private HTTP/JSON protocol between
RemoteWorldAdapter and a Maho world service. It does not add or change any
public AgentBridge endpoint or Agent Protocol v1 field.

The adapter protocol version string is `1.0`. A service must reject an
incompatible version. All request and response bodies use
`application/json; charset=utf-8`.

The words MUST, MUST NOT, SHOULD, and MAY describe protocol requirements.

## JSON and identity rules

- `request_id`, `session_id`, `tool_call_id`, and `undo_token` are UUID
  strings where present.
- `world_id` is a non-empty string of at most 128 characters.
- Revisions are non-negative safe integers.
- Numbers MUST be finite.
- DTOs MUST contain only JSON values and plain objects.
- Unknown request fields are rejected (`additionalProperties: false`).
- Prototype-pollution keys (`__proto__`, `prototype`, and `constructor`) are
  rejected at any depth.
- A response MUST echo `request_id`, `session_id`, and `world_id` for the
  corresponding operation.
- ToolResults MUST correlate to requested ToolCalls by `tool_call_id` and
  preserve request order.

The client limits a response body to 4 MiB. The included fake server limits a
request body to 1 MiB.

WorldSnapshot is a decision DTO, not shared memory. It MUST NOT contain engine
object references, memory addresses, Node objects, or C++ pointers.

## Core DTOs

Entity DTOs reuse the current Agent Core shape:

```json
{
  "entity_id": "entity-identifier",
  "generation": 1,
  "name": "ProtocolCube",
  "entity_type": "primitive",
  "primitive_type": "cube",
  "transform": {
    "position": [0, 0, 0],
    "rotation": [0, 0, 0],
    "scale": [1, 1, 1]
  },
  "properties": {
    "color": [1, 1, 1, 1],
    "visible": true,
    "label": ""
  }
}
```

`generation` is a non-negative integer, `entity_id` is non-empty, and all
Transform values are finite. Position and rotation are three-number vectors.
Scale is a three-number vector using the existing tool schema limits.

For protocol v1, `entity_id` is the authoritative command-addressing identity.
A service MUST NOT reuse an `entity_id` during one world lifecycle and MUST NOT
use a raw pointer address as an ID. `generation` is snapshot diagnostic and
future-compatibility data; it does not replace or augment `entity_id` in v1
ToolCall arguments. Supporting reusable IDs that require generation-aware
addressing would require a separate protocol and tool-schema upgrade.

A ToolCall is:

```json
{
  "tool_call_id": "3220ff99-f9d2-41ce-910b-e5956eb04883",
  "tool_name": "entity.set_transform",
  "args": {
    "entity_id": "entity-identifier",
    "transform": {
      "position": [3, 1, 5]
    }
  }
}
```

ToolResult fields are shown in the execute response below. Its request and call
IDs, revisions, changes, undo token, error, data, and dry-run flag are
authoritative server output.

A ChangeSet is an ordered array of plain change DTOs. Every change includes
`operation`, `before`, and `after`; entity/property identifiers may be added
where applicable. Change data must remain JSON-only and describe the committed
transaction result. AgentBridge does not reconstruct missing changes.

## Authentication and transport

The client accepts only credential-free `http` or `https` base URLs without a
query string or fragment. `127.0.0.1`, `localhost`, and `::1` are allowed by
default.

Non-loopback access requires:

```text
MAHO_WORLD_ALLOW_NON_LOOPBACK=1
MAHO_WORLD_AUTH_TOKEN=<non-empty token>
```

When configured, the token is sent only as:

```http
Authorization: Bearer <token>
```

The token MUST NOT appear in JSON, URLs, metadata, logs, error details, or
audit records.

## Capabilities

The capability DTO is:

```json
{
  "supports_atomic_transactions": true,
  "supports_dry_run": true,
  "supports_undo": true,
  "supports_idempotency": true,
  "max_tool_calls": 16,
  "supported_tools": [
    "world.get_summary",
    "world.query_entities",
    "entity.get",
    "entity.spawn_primitive",
    "entity.destroy",
    "entity.set_transform",
    "entity.set_property",
    "history.undo"
  ]
}
```

Each `supports_*` field is an honest boolean and MAY be `true` or `false`.
`max_tool_calls` is an integer from 1 through the protocol safety limit of
1000. `supported_tools` MUST be a non-empty, duplicate-free subset of the
current ToolRegistry names; an unknown name is a capability error.

The dependency rules are:

- `supports_undo: false` forbids `history.undo` in `supported_tools`;
- `supports_undo: true` requires `history.undo` in `supported_tools`;
- `supports_atomic_transactions: false` requires `max_tool_calls: 1` in Agent
  Core v0.4.1;
- every ToolCall name MUST appear in `supported_tools`;
- every request MUST contain no more than `max_tool_calls` ToolCalls;
- more than one ToolCall requires `supports_atomic_transactions: true` and an
  atomic request;
- `dry_run: true` requires `supports_dry_run: true`;
- undo execution requires `supports_undo: true`.

AgentBridge enforces these rules locally before world execution. An
unsupported request is rejected; it is never rewritten as a real execution,
split into independent remote requests, inferred from Provider text, routed to
another adapter, or retried against MockWorldAdapter. Capability declarations
that violate these dependencies fail health negotiation.

`supports_idempotency: false` remains expressible by the v1 DTO. The current
RemoteWorldAdapter does not accept such a remote profile because a transport
failure may occur after commit and AgentBridge request replay requires the
server to return the original semantic result for the same request ID. This is
a safety requirement of the current remote implementation, not a new protocol
version.

## Minimal World Profile

The non-mandatory Minimal World Profile for the first Maho C++ integration is:

```json
{
  "supports_atomic_transactions": false,
  "supports_dry_run": false,
  "supports_undo": false,
  "supports_idempotency": true,
  "max_tool_calls": 1,
  "supported_tools": [
    "world.get_summary",
    "entity.spawn_primitive",
    "entity.set_transform"
  ]
}
```

This profile is Protocol v1, not Protocol v2. Every execute request contains
exactly one ToolCall and uses non-atomic single-operation semantics. It cannot
execute `history.undo` or dry-run. Limited entity query support also limits the
Provider prompt and Session reference context; a successful spawn therefore
MUST return its real `entity_id` in the authoritative ToolResult so a later
`entity.set_transform` can address it. Full adapters may continue to advertise
all eight tools and every capability.

## Error DTO

Errors use the existing Agent Protocol error shape:

```json
{
  "code": "REVISION_CONFLICT",
  "message": "Expected revision 2, current revision is 3",
  "details": {
    "expected_revision": 2,
    "current_revision": 3
  },
  "retryable": true
}
```

An operation result has `error: null` on success and an Error DTO on failure.
Servers SHOULD use:

- HTTP 400/422 for invalid requests or arguments;
- HTTP 401/403 for authentication or permission failure;
- HTTP 404 for unknown routes;
- HTTP 408 for a server-side timeout;
- HTTP 409 for revision conflict;
- HTTP 429 for overload;
- HTTP 500/503 for server or availability failure.

Clients validate a structured transaction failure even when its HTTP status is
non-2xx. HTTP 408 maps to timeout, 409 to revision conflict, and 429/5xx are
retryable error reports. RemoteWorldAdapter itself performs no retry.

## Health

### Request

```http
GET /world-adapter/v1/health
Accept: application/json
```

### Success response

```json
{
  "ok": true,
  "adapter_protocol_version": "1.0",
  "server_name": "maho-world",
  "server_version": "1.0.0",
  "capabilities": {
    "supports_atomic_transactions": true,
    "supports_dry_run": true,
    "supports_undo": true,
    "supports_idempotency": true,
    "max_tool_calls": 16,
    "supported_tools": [
      "world.get_summary",
      "world.query_entities",
      "entity.get",
      "entity.spawn_primitive",
      "entity.destroy",
      "entity.set_transform",
      "entity.set_property",
      "history.undo"
    ]
  },
  "error": null
}
```

Health does not create a Session or world. Clients MAY cache a valid health
response briefly; Agent Core v0.4.1 caches it for one second.

## Snapshot

### Request

```http
POST /world-adapter/v1/snapshot
Content-Type: application/json
```

```json
{
  "adapter_protocol_version": "1.0",
  "request_id": "c4bc2938-0442-4a4d-8745-79f6959c1439",
  "session_id": "e90363d2-ea8a-46e9-a1ed-3c7946ddb565",
  "world_id": "713c102c-37b1-48cc-a14f-faa9f752b301"
}
```

### Success response

```json
{
  "ok": true,
  "adapter_protocol_version": "1.0",
  "request_id": "c4bc2938-0442-4a4d-8745-79f6959c1439",
  "session_id": "e90363d2-ea8a-46e9-a1ed-3c7946ddb565",
  "world_id": "713c102c-37b1-48cc-a14f-faa9f752b301",
  "world_revision": 7,
  "timestamp_ms": 1785484800000,
  "capabilities": {
    "supports_atomic_transactions": true,
    "supports_dry_run": true,
    "supports_undo": true,
    "supports_idempotency": true,
    "max_tool_calls": 16,
    "supported_tools": [
      "world.get_summary",
      "world.query_entities",
      "entity.get",
      "entity.spawn_primitive",
      "entity.destroy",
      "entity.set_transform",
      "entity.set_property",
      "history.undo"
    ]
  },
  "entities": [],
  "history": [],
  "error": null
}
```

`timestamp_ms` is a non-negative integer. `entities` is required. `history`
MAY be omitted and is normalized to an empty array. The response revision and
entity data are authoritative.

## Execute transaction

### Request

```http
POST /world-adapter/v1/execute
Content-Type: application/json
```

```json
{
  "adapter_protocol_version": "1.0",
  "request_id": "7aa83fae-ea40-46e0-9f6a-b0d23b85c09c",
  "session_id": "e90363d2-ea8a-46e9-a1ed-3c7946ddb565",
  "world_id": "713c102c-37b1-48cc-a14f-faa9f752b301",
  "expected_revision": 7,
  "dry_run": false,
  "atomic": true,
  "tool_calls": [
    {
      "tool_call_id": "3220ff99-f9d2-41ce-910b-e5956eb04883",
      "tool_name": "entity.spawn_primitive",
      "args": {
        "primitive_type": "cube",
        "name": "ProtocolCube"
      }
    }
  ]
}
```

`atomic` is a required boolean. Multiple ToolCalls require `atomic: true` and
`supports_atomic_transactions: true`. A single ToolCall for a non-atomic
adapter uses `atomic: false`; AgentBridge never splits a batch to emulate an
atomic transaction. `tool_calls` contains 1 through the advertised
`max_tool_calls`. `history.undo` is not executed through this endpoint by
Agent Core; it uses the undo endpoint.

### Success response

```json
{
  "ok": true,
  "adapter_protocol_version": "1.0",
  "request_id": "7aa83fae-ea40-46e0-9f6a-b0d23b85c09c",
  "session_id": "e90363d2-ea8a-46e9-a1ed-3c7946ddb565",
  "world_id": "713c102c-37b1-48cc-a14f-faa9f752b301",
  "before_revision": 7,
  "after_revision": 8,
  "replayed": false,
  "tool_results": [
    {
      "ok": true,
      "request_id": "7aa83fae-ea40-46e0-9f6a-b0d23b85c09c",
      "tool_call_id": "3220ff99-f9d2-41ce-910b-e5956eb04883",
      "before_revision": 7,
      "after_revision": 8,
      "changes": [],
      "undo_token": "5a366926-c3c4-4d4e-83d2-6e1c0deba0e6",
      "error": null,
      "data": {
        "entity": {}
      },
      "dry_run": false
    }
  ],
  "changes": [],
  "undo_token": "5a366926-c3c4-4d4e-83d2-6e1c0deba0e6",
  "error": null,
  "failed_tool_call_index": null
}
```

A successful transaction returns exactly one ToolResult per ToolCall. All
ToolResults use the transaction's before and after revisions. A read-only
transaction does not advance the revision and returns no undo token.

### Failure response

```json
{
  "ok": false,
  "adapter_protocol_version": "1.0",
  "request_id": "7aa83fae-ea40-46e0-9f6a-b0d23b85c09c",
  "session_id": "e90363d2-ea8a-46e9-a1ed-3c7946ddb565",
  "world_id": "713c102c-37b1-48cc-a14f-faa9f752b301",
  "before_revision": 7,
  "after_revision": 7,
  "replayed": false,
  "tool_results": [
    {
      "ok": false,
      "request_id": "7aa83fae-ea40-46e0-9f6a-b0d23b85c09c",
      "tool_call_id": "3220ff99-f9d2-41ce-910b-e5956eb04883",
      "before_revision": 7,
      "after_revision": 7,
      "changes": [],
      "undo_token": null,
      "error": {
        "code": "EXECUTION_FAILED",
        "message": "Transaction failed",
        "details": {},
        "retryable": false
      },
      "data": null,
      "dry_run": false
    }
  ],
  "changes": [],
  "undo_token": null,
  "error": {
    "code": "EXECUTION_FAILED",
    "message": "Transaction failed",
    "details": {},
    "retryable": false
  },
  "failed_tool_call_index": 0
}
```

An atomic failure MUST leave the world at `before_revision`, report no
committed changes, and return no undo token.

## Undo

### Request

```http
POST /world-adapter/v1/undo
Content-Type: application/json
```

```json
{
  "adapter_protocol_version": "1.0",
  "request_id": "00addb0d-d23c-425e-9190-3679ed5ed67c",
  "session_id": "e90363d2-ea8a-46e9-a1ed-3c7946ddb565",
  "world_id": "713c102c-37b1-48cc-a14f-faa9f752b301",
  "expected_revision": 8,
  "undo_token": "5a366926-c3c4-4d4e-83d2-6e1c0deba0e6"
}
```

### Success response

```json
{
  "ok": true,
  "adapter_protocol_version": "1.0",
  "request_id": "00addb0d-d23c-425e-9190-3679ed5ed67c",
  "session_id": "e90363d2-ea8a-46e9-a1ed-3c7946ddb565",
  "world_id": "713c102c-37b1-48cc-a14f-faa9f752b301",
  "before_revision": 8,
  "after_revision": 9,
  "replayed": false,
  "changes": [],
  "undo_token": null,
  "error": null,
  "data": {
    "undone_token": "5a366926-c3c4-4d4e-83d2-6e1c0deba0e6"
  }
}
```

A successful undo is a new write and advances the revision exactly once. The
consumed token cannot be reused with a new request ID. Reusing the same
successful request ID returns the cached result with `replayed: true`.

## Revision, transaction, dry-run, and idempotency rules

1. The server compares `expected_revision` with the authoritative revision
   before execution.
2. A mismatch returns `REVISION_CONFLICT` without executing tools.
3. A successful write transaction advances the revision once, regardless of
   the number of ToolCalls.
4. A read-only transaction does not advance the revision.
5. `dry_run: true` validates and predicts the transaction but restores the
   world, does not advance the revision, and returns no undo token.
6. Atomic execution rolls back all earlier calls if any call fails.
7. Idempotency is scoped to the Session/world and operation. Repeating a
   request ID returns the original semantic result with `replayed: true`, even
   if the supplied expected revision is now stale.
8. A request ID MUST NOT be reused for a different operation.
9. Session/world pairs are isolated from every other pair.

If dry-run is not advertised, AgentBridge rejects `dry_run: true` locally and
does not send an execute request. If undo is not advertised, the public
AgentBridge undo endpoint remains available but returns `UNDO_NOT_AVAILABLE`
without sending a remote undo request. These rejections do not advance the
revision, create an undo token, or update EntityContext.

## Cancellation and failure safety

The client supports an external AbortSignal, a per-request timeout, and
shutdown cancellation. Cancellation never causes local Mock execution and
never triggers fallback. Because a connection can fail after a remote service
has committed, transport failure can represent an uncertain remote outcome;
callers should reconcile with an authoritative snapshot and the same request ID
rather than issue a different write automatically.

An invalid or uncorrelated response is never used to update Agent Core
EntityContext. Raw remote bodies and credentials are never copied into public
errors or audit records.

Deterministic protocol fixtures live in
`tests/fixtures/world-adapter-v1/`. Node tests validate them through the same
runtime contracts used in production, including explicit rejection tests for
invalid undo dependencies and unknown tool capabilities. They are intended to
be shared with future C++ conformance tests.

## C++ thread model

A future game implementation must receive HTTP on an I/O thread and enqueue a
validated command transaction for a bounded main-thread execution phase. Only
the game main thread may resolve World/Entity objects, mutate transforms or
properties, advance revisions, build ChangeSets, and create or consume
engine-native undo tokens. The HTTP thread must never mutate the World
directly. The game service is the sole authority; AgentBridge only proposes
validated transactions and consumes DTO results.
