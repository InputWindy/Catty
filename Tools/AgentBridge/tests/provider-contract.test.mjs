import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import test from "node:test";
import {
  DEFAULT_PROVIDER_CAPABILITIES,
  EMPTY_USAGE,
  assertProviderContract,
  createProviderOutput,
  validateProviderOutput,
} from "../src/agent/provider-contract.mjs";
import {
  assertNormalizedMessages,
  createPlanMessages,
} from "../src/agent/normalized-messages.mjs";

test("normalized messages support system, user, assistant, and tool roles", () => {
  const tool_call_id = randomUUID();
  const messages = [
    { role: "system", content: "system" },
    { role: "user", content: "user" },
    {
      role: "assistant",
      content: "",
      tool_calls: [
        {
          tool_call_id,
          tool_name: "world.get_summary",
          args: {},
        },
      ],
    },
    {
      role: "tool",
      tool_call_id,
      name: "world.get_summary",
      content: "{\"ok\":true}",
    },
  ];
  assert.equal(assertNormalizedMessages(messages), messages);
  assert.equal(
    createPlanMessages({
      system_message: "system",
      user_message: "hello",
    }).length,
    2
  );
});

test("normalized messages reject prototype pollution keys", () => {
  const args = JSON.parse('{"__proto__":{"polluted":true}}');
  assert.throws(
    () =>
      assertNormalizedMessages([
        {
          role: "assistant",
          content: "",
          tool_calls: [
            {
              tool_call_id: randomUUID(),
              tool_name: "world.get_summary",
              args,
            },
          ],
        },
      ]),
    /forbidden key/
  );
});

test("provider contract requires capabilities, plan, and metadata", () => {
  const provider = {
    name: "test",
    model: "test-model",
    capabilities: { ...DEFAULT_PROVIDER_CAPABILITIES },
    async plan() {},
    getMetadata() {
      return {};
    },
  };
  assert.equal(assertProviderContract(provider), provider);
  assert.throws(
    () => assertProviderContract({ ...provider, plan: undefined }),
    /plan/
  );
});

test("provider output normalizes usage and validates ToolCalls at runtime", () => {
  const output = createProviderOutput({
    provider: "test",
    model: "test-model",
    assistant_message: "planned",
    tool_calls: [
      {
        tool_call_id: randomUUID(),
        tool_name: "entity.spawn_primitive",
        args: { primitive_type: "cube" },
      },
    ],
  });
  assert.deepEqual(output.usage, EMPTY_USAGE);
  assert.equal(
    validateProviderOutput(output, {
      expected_provider: "test",
      expected_model: "test-model",
    }),
    output
  );
});

test("provider output rejects unsafe metadata and finalization ToolCalls", () => {
  const base = {
    provider: "test",
    model: "test-model",
    assistant_message: "",
    tool_calls: [],
    finish_reason: "stop",
    usage: { ...EMPTY_USAGE },
    provider_metadata: {},
  };
  assert.throws(
    () =>
      validateProviderOutput({
        ...base,
        provider_metadata: { authorization: "Bearer secret" },
      }),
    /forbidden field/
  );
  assert.throws(
    () =>
      validateProviderOutput(
        {
          ...base,
          tool_calls: [
            {
              tool_call_id: randomUUID(),
              tool_name: "world.get_summary",
              args: {},
            },
          ],
        },
        { phase: "finalize" }
      ),
    /finalization returned/
  );
});
