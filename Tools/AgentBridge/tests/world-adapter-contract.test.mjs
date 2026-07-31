import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import test from "node:test";
import { createDefaultToolRegistry } from "../src/tools/definitions.mjs";
import {
  assertJsonDto,
  assertWorldAdapterContract,
  validateExecuteTransactionInput,
  validateExecuteTransactionResult,
  validateWorldSnapshot,
} from "../src/world/world-adapter-contract.mjs";
import { WorldAdapterError } from "../src/world/world-adapter-errors.mjs";
import { MockWorldAdapter } from "../src/world/mock-world-adapter.mjs";

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
