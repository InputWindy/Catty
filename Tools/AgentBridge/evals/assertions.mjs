import assert from "node:assert/strict";

export class EvalAssertionError extends Error {
  constructor(label, expected, actual) {
    super(
      `${label}\n  expected: ${formatValue(expected)}\n  actual:   ${formatValue(actual)}`
    );
    this.name = "EvalAssertionError";
    this.expected = expected;
    this.actual = actual;
  }
}

function formatValue(value) {
  return JSON.stringify(value);
}

function assertEqual(actual, expected, label) {
  try {
    assert.deepEqual(actual, expected);
  } catch {
    throw new EvalAssertionError(label, expected, actual);
  }
}

function assertIncludes(actual, expected, label) {
  const values = Array.isArray(expected) ? expected : [expected];
  for (const value of values) {
    if (!actual.includes(value)) {
      throw new EvalAssertionError(label, `text containing ${value}`, actual);
    }
  }
}

function assertPartial(actual, expected, label) {
  if (
    expected === null ||
    typeof expected !== "object" ||
    Array.isArray(expected)
  ) {
    assertEqual(actual, expected, label);
    return;
  }
  if (!actual || typeof actual !== "object" || Array.isArray(actual)) {
    throw new EvalAssertionError(label, expected, actual);
  }
  for (const [key, value] of Object.entries(expected)) {
    assertPartial(actual[key], value, `${label}.${key}`);
  }
}

function findEntity(snapshot, expectation) {
  const selector = expectation.entity_id
    ? (entity) => entity.entity_id === expectation.entity_id
    : expectation.name
      ? (entity) => entity.name === expectation.name
      : expectation.primitive_type
        ? (entity) => entity.primitive_type === expectation.primitive_type
        : () => true;
  const matches = snapshot.entities.filter(selector);
  if (matches.length !== 1) {
    throw new EvalAssertionError(
      "entity selector match count",
      1,
      matches.length
    );
  }
  return matches[0];
}

export function assertTurn({
  expectation,
  result,
  provider_output,
  before_snapshot,
  after_snapshot,
}) {
  if (expectation.ok !== undefined) {
    assertEqual(result.ok, expectation.ok, "result.ok");
  }
  if (expectation.assistant_includes !== undefined) {
    assertIncludes(
      result.assistant_message,
      expectation.assistant_includes,
      "assistant_message"
    );
  }

  const tool_names = provider_output.tool_calls.map(
    (tool_call) => tool_call.tool_name
  );
  if (expectation.tool_names !== undefined) {
    assertEqual(tool_names, expectation.tool_names, "tool names");
  }
  if (expectation.tool_call_count !== undefined) {
    assertEqual(
      provider_output.tool_calls.length,
      expectation.tool_call_count,
      "tool call count"
    );
  }
  if (expectation.revision !== undefined) {
    assertEqual(result.world_revision, expectation.revision, "world revision");
  }
  if (expectation.revision_delta !== undefined) {
    assertEqual(
      after_snapshot.revision - before_snapshot.revision,
      expectation.revision_delta,
      "world revision delta"
    );
  }
  if (expectation.entity_count !== undefined) {
    assertEqual(
      after_snapshot.entities.length,
      expectation.entity_count,
      "entity count"
    );
  }
  if (expectation.undo_created !== undefined) {
    assertEqual(
      result.undo_token !== null,
      expectation.undo_created,
      "undo token created"
    );
  }
  if (expectation.no_world_change === true) {
    assertEqual(after_snapshot, before_snapshot, "world snapshot");
  }
  if (expectation.clarifies === true) {
    assertEqual(provider_output.tool_calls.length, 0, "clarification tool calls");
    assertIncludes(
      result.assistant_message,
      ["请", "明确"],
      "clarification assistant_message"
    );
    assertEqual(after_snapshot, before_snapshot, "clarification world snapshot");
    assertEqual(result.undo_token, null, "clarification undo token");
  }
  if (expectation.entity !== undefined) {
    const entity = findEntity(after_snapshot, expectation.entity);
    assertPartial(entity, expectation.entity, "entity");
  }
}

export function assertFinal(expectation, snapshot) {
  if (!expectation) {
    return;
  }
  if (expectation.revision !== undefined) {
    assertEqual(snapshot.revision, expectation.revision, "final revision");
  }
  if (expectation.entity_count !== undefined) {
    assertEqual(
      snapshot.entities.length,
      expectation.entity_count,
      "final entity count"
    );
  }
  for (const entity_expectation of expectation.entities || []) {
    const entity = findEntity(snapshot, entity_expectation);
    assertPartial(entity, entity_expectation, "final entity");
  }
}
