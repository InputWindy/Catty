import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import test from "node:test";
import { createDefaultToolRegistry } from "../src/tools/definitions.mjs";
import {
  assertJsonDto,
  assertWorldAdapterContract,
  validateAdapterCapabilities,
  validateAdapterCapabilityRequest,
  validateExecuteTransactionInput,
  validateExecuteTransactionResult,
  validateWorldSnapshot,
} from "../src/world/world-adapter-contract.mjs";
import { WorldAdapterError } from "../src/world/world-adapter-errors.mjs";
import { MockWorldAdapter } from "../src/world/mock-world-adapter.mjs";

const MINIMAL_TOOL_NAMES = [
  "world.get_summary",
  "entity.spawn_primitive",
  "entity.set_transform",
];

function fullCapabilities() {
  return {
    supports_atomic_transactions: true,
    supports_dry_run: true,
    supports_undo: true,
    supports_idempotency: true,
    max_tool_calls: 16,
    supported_tools: createDefaultToolRegistry()
      .listDefinitions()
      .map((definition) => definition.name),
  };
}

function minimalCapabilities(overrides = {}) {
  return {
    supports_atomic_transactions: false,
    supports_dry_run: false,
    supports_undo: false,
    supports_idempotency: true,
    max_tool_calls: 1,
    supported_tools: [...MINIMAL_TOOL_NAMES],
    ...overrides,
  };
}

function knownToolNames() {
  return createDefaultToolRegistry()
    .listDefinitions()
    .map((definition) => definition.name);
}

function transactionInput(overrides = {}) {
  return {
    request_id: randomUUID(),
    session_id: randomUUID(),
    world_id: randomUUID(),
    expected_revision: 0,
    dry_run: false,
    atomic: true,
    tool_calls: [
      {
        tool_call_id: randomUUID(),
        tool_name: "world.get_summary",
        args: {},
      },
    ],
    ...overrides,
  };
}

test("WorldAdapter contract requires identity, capabilities, lifecycle methods, and safe metadata", () => {
  const session_id = randomUUID();
  const world_id = randomUUID();
  const adapter = new MockWorldAdapter({
    session_id,
    world_id,
    tool_registry: createDefaultToolRegistry(),
  });

  assert.equal(assertWorldAdapterContract(adapter), adapter);
  assert.equal(adapter.name, "mock");
  assert.equal(adapter.protocolVersion, "1.0");
  assert.equal(adapter.capabilities.supports_atomic_transactions, true);
  assert.equal(adapter.getMetadata().world_id, world_id);
});

test("WorldAdapter capability contract accepts full and minimal profiles", () => {
  const known_tools = knownToolNames();
  assert.deepEqual(
    validateAdapterCapabilities(minimalCapabilities(), { known_tools }),
    minimalCapabilities()
  );
  assert.deepEqual(
    validateAdapterCapabilities(fullCapabilities(), { known_tools }),
    fullCapabilities()
  );
  assert.equal(
    validateAdapterCapabilities(
      minimalCapabilities({ supports_idempotency: false }),
      { known_tools }
    ).supports_idempotency,
    false
  );
});

test("WorldAdapter capability contract rejects invalid tool subsets", () => {
  const known_tools = knownToolNames();
  for (const capabilities of [
    minimalCapabilities({ supported_tools: [] }),
    minimalCapabilities({
      supported_tools: ["world.get_summary", "world.get_summary"],
    }),
    minimalCapabilities({
      supported_tools: ["world.get_summary", "unknown.tool"],
    }),
  ]) {
    assert.throws(
      () => validateAdapterCapabilities(capabilities, { known_tools }),
      (error) =>
        error instanceof WorldAdapterError &&
        error.reason === "capability_insufficient"
    );
  }
});

test("WorldAdapter capability contract enforces undo and atomic dependencies", () => {
  const known_tools = knownToolNames();
  assert.throws(
    () =>
      validateAdapterCapabilities(
        minimalCapabilities({
          supported_tools: [...MINIMAL_TOOL_NAMES, "history.undo"],
        }),
        { known_tools }
      ),
    /requires supports_undo=true/
  );
  assert.throws(
    () =>
      validateAdapterCapabilities(
        {
          ...fullCapabilities(),
          supported_tools: fullCapabilities().supported_tools.filter(
            (name) => name !== "history.undo"
          ),
        },
        { known_tools }
      ),
    /requires history.undo/
  );
  assert.throws(
    () =>
      validateAdapterCapabilities(
        minimalCapabilities({ max_tool_calls: 2 }),
        { known_tools }
      ),
    /max_tool_calls=1/
  );
});

test("WorldAdapter capability request validation rejects unsupported semantics", () => {
  const known_tools = knownToolNames();
  const call = (tool_name) => ({ tool_name });

  assert.throws(
    () =>
      validateAdapterCapabilityRequest(minimalCapabilities(), {
        known_tools,
        tool_calls: [call("entity.destroy")],
      }),
    /does not advertise/
  );
  assert.throws(
    () =>
      validateAdapterCapabilityRequest(minimalCapabilities(), {
        known_tools,
        tool_calls: [
          call("world.get_summary"),
          call("entity.spawn_primitive"),
        ],
        atomic: true,
      }),
    /exceeds/
  );
  assert.throws(
    () =>
      validateAdapterCapabilityRequest(minimalCapabilities(), {
        known_tools,
        tool_calls: [call("world.get_summary")],
        dry_run: true,
      }),
    /dry-run/
  );
  assert.throws(
    () =>
      validateAdapterCapabilityRequest(minimalCapabilities(), {
        known_tools,
        tool_calls: [call("world.get_summary")],
        atomic: true,
      }),
    /atomic/
  );
  assert.throws(
    () =>
      validateAdapterCapabilityRequest(minimalCapabilities(), {
        known_tools,
        tool_calls: [call("history.undo")],
      }),
    (error) =>
      error instanceof WorldAdapterError &&
      error.reason === "undo_not_available"
  );
});

test("WorldAdapter input validation rejects unknown fields and non-JSON DTO values", () => {
  assert.throws(
    () =>
      validateExecuteTransactionInput(
        transactionInput({ unexpected: true })
      ),
    (error) =>
      error instanceof WorldAdapterError &&
      error.reason === "invalid_argument"
  );

  assert.throws(
    () =>
      validateExecuteTransactionInput(
        transactionInput({
          tool_calls: [
            {
              tool_call_id: randomUUID(),
              tool_name: "world.get_summary",
              args: { unsafe: new Map() },
            },
          ],
        })
      ),
    (error) =>
      error instanceof WorldAdapterError &&
      error.reason === "response_invalid"
  );

  assert.throws(
    () => assertJsonDto({ value: Number.POSITIVE_INFINITY }),
    /non-finite/
  );
  assert.throws(
    () => assertJsonDto(JSON.parse('{"__proto__":{"polluted":true}}')),
    /forbidden key/
  );
});

test("WorldAdapter snapshot validation rejects invalid entities and mismatched worlds", () => {
  const world_id = randomUUID();
  const snapshot = {
    world_id,
    revision: 0,
    entities: [],
    history: [],
  };
  assert.deepEqual(
    validateWorldSnapshot(snapshot, { expected_world_id: world_id }),
    snapshot
  );
  assert.throws(
    () =>
      validateWorldSnapshot(snapshot, {
        expected_world_id: randomUUID(),
      }),
    (error) =>
      error instanceof WorldAdapterError &&
      error.reason === "correlation_mismatch"
  );
  assert.throws(
    () =>
      validateWorldSnapshot({
        ...snapshot,
        entities: [
          {
            entity_id: "",
            generation: -1,
          },
        ],
      }),
    /contract validation/
  );
});

test("WorldAdapter execute result validation enforces correlation and ToolResult identity", () => {
  const input = transactionInput();
  const base = {
    ok: true,
    request_id: input.request_id,
    session_id: input.session_id,
    world_id: input.world_id,
    before_revision: 0,
    after_revision: 0,
    replayed: false,
    tool_results: [
      {
        ok: true,
        request_id: input.request_id,
        tool_call_id: input.tool_calls[0].tool_call_id,
        before_revision: 0,
        after_revision: 0,
        changes: [],
        undo_token: null,
        error: null,
        data: {},
        dry_run: false,
      },
    ],
    changes: [],
    undo_token: null,
    error: null,
  };
  assert.deepEqual(validateExecuteTransactionResult(base, input), base);

  assert.throws(
    () =>
      validateExecuteTransactionResult(
        { ...base, request_id: randomUUID() },
        input
      ),
    (error) =>
      error instanceof WorldAdapterError &&
      error.reason === "correlation_mismatch"
  );
  assert.throws(
    () =>
      validateExecuteTransactionResult(
        {
          ...base,
          tool_results: [
            {
              ...base.tool_results[0],
              tool_call_id: randomUUID(),
            },
          ],
        },
        input
      ),
    /correlated/
  );
});
