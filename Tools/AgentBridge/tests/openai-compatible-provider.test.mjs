import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import test from "node:test";
import { AgentService } from "../src/agent/agent-service.mjs";
import { createPlanMessages } from "../src/agent/normalized-messages.mjs";
import { OpenAICompatibleProvider } from "../src/agent/providers/openai-compatible-provider.mjs";
import { ProviderError } from "../src/agent/providers/provider-errors.mjs";
import { createDefaultToolRegistry } from "../src/tools/definitions.mjs";
import {
  completion,
  providerToolCall,
  startFakeOpenAIServer,
} from "./helpers/fake-openai-server.mjs";
import { createTestCore } from "./helpers/core.mjs";

function input(overrides = {}) {
  const tool_definitions = createDefaultToolRegistry().listDefinitions();
  return {
    request_id: randomUUID(),
    session_id: randomUUID(),
    user_message: "create a cube",
    normalized_messages: createPlanMessages({
      system_message: "system",
      user_message: "create a cube",
    }),
    world_snapshot: {
      world_id: randomUUID(),
      revision: 0,
      entities: [],
      history: [],
    },
    session_context: {},
    tool_definitions,
    ...overrides,
  };
}

function provider(base_url, overrides = {}) {
  return new OpenAICompatibleProvider({
    provider_id: "openai-compatible",
    base_url,
    model: "test-model",
    api_key: "test-secret-key",
    max_retries: 0,
    timeout_ms: 1_000,
    ...overrides,
  });
}

test("generic Provider sends standard Chat Completions and parses text usage", async (t) => {
  const fake = await startFakeOpenAIServer(() => ({
    body: completion({
      content: "hello",
      usage: {
        prompt_tokens: 10,
        completion_tokens: 3,
        total_tokens: 13,
        prompt_tokens_details: { cached_tokens: 4 },
      },
    }),
  }));
  t.after(() => fake.close());
  const output = await provider(fake.base_url).plan(input());

  assert.equal(output.assistant_message, "hello");
  assert.deepEqual(output.usage, {
    input_tokens: 10,
    output_tokens: 3,
    total_tokens: 13,
    cached_input_tokens: 4,
  });
  assert.equal(fake.requests[0].url, "/v1/chat/completions");
  assert.equal(
    fake.requests[0].headers.authorization,
    "Bearer test-secret-key"
  );
  assert.equal(fake.requests[0].body.stream, false);
  assert.equal(fake.requests[0].body.tool_choice, "auto");
  assert.equal("thinking" in fake.requests[0].body, false);
  assert.equal(fake.requests[0].body.tools.length, 8);
});

test("Provider parses single and multiple ToolCalls and preserves valid UUID ids", async (t) => {
  const preserved_id = randomUUID();
  const fake = await startFakeOpenAIServer(() => ({
    body: completion({
      content: null,
      finish_reason: "tool_calls",
      tool_calls: [
        providerToolCall({
          id: preserved_id,
          name: "entity__spawn_primitive",
          arguments: '{"primitive_type":"cube"}',
        }),
        providerToolCall({
          id: "call_not_an_internal_uuid",
          name: "world__get_summary",
          arguments: "{}",
        }),
      ],
    }),
  }));
  t.after(() => fake.close());
  const output = await provider(fake.base_url).plan(input());

  assert.equal(output.tool_calls.length, 2);
  assert.equal(output.tool_calls[0].tool_call_id, preserved_id);
  assert.equal(output.tool_calls[0].tool_name, "entity.spawn_primitive");
  assert.deepEqual(output.tool_calls[0].args, { primitive_type: "cube" });
  assert.match(output.tool_calls[1].tool_call_id, /^[0-9a-f-]{36}$/);
  assert.equal(output.tool_calls[1].tool_name, "world.get_summary");
});

for (const scenario of [
  {
    name: "invalid JSON arguments",
    tool_call: providerToolCall({
      name: "world__get_summary",
      arguments: "{invalid",
    }),
    reason: "tool_arguments_invalid",
  },
  {
    name: "non-object arguments",
    tool_call: providerToolCall({
      name: "world__get_summary",
      arguments: "[]",
    }),
    reason: "tool_arguments_invalid",
  },
  {
    name: "unknown tool",
    tool_call: providerToolCall({
      name: "shell__execute",
      arguments: "{}",
    }),
    reason: "unknown_tool",
  },
]) {
  test(`Provider rejects ${scenario.name}`, async (t) => {
    const fake = await startFakeOpenAIServer(() => ({
      body: completion({ tool_calls: [scenario.tool_call] }),
    }));
    t.after(() => fake.close());
    await assert.rejects(
      provider(fake.base_url).plan(input()),
      (error) =>
        error instanceof ProviderError && error.reason === scenario.reason
    );
  });
}

test("Provider rejects the entire response when ToolCall count exceeds the limit", async (t) => {
  const fake = await startFakeOpenAIServer(() => ({
    body: completion({
      tool_calls: [
        providerToolCall({
          name: "world__get_summary",
          arguments: "{}",
        }),
        providerToolCall({
          name: "world__get_summary",
          arguments: "{}",
        }),
      ],
    }),
  }));
  t.after(() => fake.close());
  await assert.rejects(
    provider(fake.base_url, { max_tool_calls: 1 }).plan(input()),
    (error) =>
      error instanceof ProviderError &&
      error.reason === "tool_call_limit_exceeded"
  );
});

test("finalization omits tools and rejects returned ToolCalls", async (t) => {
  let call = 0;
  const fake = await startFakeOpenAIServer(() => {
    call += 1;
    return {
      body:
        call === 1
          ? completion({ content: "final" })
          : completion({
              tool_calls: [
                providerToolCall({
                  name: "world__get_summary",
                  arguments: "{}",
                }),
              ],
            }),
    };
  });
  t.after(() => fake.close());
  const client = provider(fake.base_url);
  const first = await client.finalize(input());
  assert.equal(first.assistant_message, "final");
  assert.equal("tools" in fake.requests[0].body, false);
  assert.equal("tool_choice" in fake.requests[0].body, false);
  await assert.rejects(
    client.finalize(input()),
    (error) =>
      error instanceof ProviderError &&
      error.reason === "finalization_tool_call"
  );
});

test("429 and 5xx retry within policy while 400 and 401 do not retry", async (t) => {
  for (const scenario of [
    { status: 429, retries: 1, expected_calls: 2, succeeds: true },
    { status: 500, retries: 1, expected_calls: 2, succeeds: true },
    { status: 400, retries: 2, expected_calls: 1, succeeds: false },
    { status: 401, retries: 2, expected_calls: 1, succeeds: false },
  ]) {
    let calls = 0;
    const fake = await startFakeOpenAIServer(() => {
      calls += 1;
      if (calls === 1) {
        return {
          status: scenario.status,
          headers: { "retry-after": "0" },
          body: { error: "simulated" },
        };
      }
      return { body: completion({ content: "recovered" }) };
    });
    const client = provider(fake.base_url, {
      max_retries: scenario.retries,
      sleep_impl: async () => {},
    });
    if (scenario.succeeds) {
      const output = await client.plan(input());
      assert.equal(output.assistant_message, "recovered");
      assert.equal(output.provider_metadata.attempt_count, 2);
    } else {
      await assert.rejects(client.plan(input()), ProviderError);
    }
    assert.equal(calls, scenario.expected_calls);
    await fake.close();
  }
  t.after(() => {});
});

test("timeout, external cancellation, and shutdown abort in-flight requests", async (t) => {
  const fake = await startFakeOpenAIServer(() => ({
    delay_ms: 200,
    body: completion({ content: "late" }),
  }));
  t.after(() => fake.close());

  await assert.rejects(
    provider(fake.base_url, { timeout_ms: 10 }).plan(input()),
    (error) =>
      error instanceof ProviderError &&
      error.reason === "request_timeout" &&
      error.timeout
  );

  const external = new AbortController();
  const external_request = provider(fake.base_url).plan(
    input({ signal: external.signal })
  );
  external.abort();
  await assert.rejects(
    external_request,
    (error) =>
      error instanceof ProviderError &&
      error.reason === "request_cancelled"
  );

  const closing_provider = provider(fake.base_url);
  const closing_request = closing_provider.plan(input());
  await closing_provider.close();
  await assert.rejects(
    closing_request,
    (error) =>
      error instanceof ProviderError &&
      error.reason === "request_cancelled"
  );
});

test("Provider rejects non-JSON, missing choices, and interrupted responses", async () => {
  for (const response of [
    { raw: "not-json" },
    { body: { id: "missing-choices" } },
    { destroy: true },
  ]) {
    const fake = await startFakeOpenAIServer(() => response);
    await assert.rejects(provider(fake.base_url).plan(input()), ProviderError);
    await fake.close();
  }
});

test("configuration and errors never contain the API key", async (t) => {
  const secret = "unique-secret-that-must-not-leak";
  const fake = await startFakeOpenAIServer(() => ({
    status: 401,
    body: { error: secret },
  }));
  t.after(() => fake.close());
  const client = provider(fake.base_url, { api_key: secret });
  let caught;
  try {
    await client.plan(input());
  } catch (error) {
    caught = error;
  }
  assert.equal(caught instanceof ProviderError, true);
  assert.equal(JSON.stringify(caught).includes(secret), false);
  assert.equal(caught.message.includes(secret), false);
  assert.equal(JSON.stringify(client.getMetadata()).includes(secret), false);
});

test("fake server integrates plan, CommandExecutor, and one text finalization", async (t) => {
  let request_count = 0;
  const tool_call_id = randomUUID();
  const fake = await startFakeOpenAIServer((request) => {
    request_count += 1;
    if (request_count === 1) {
      assert.equal(request.body.tools.length, 8);
      return {
        body: completion({
          content: "",
          finish_reason: "tool_calls",
          tool_calls: [
            providerToolCall({
              id: tool_call_id,
              name: "entity__spawn_primitive",
              arguments: '{"primitive_type":"cube"}',
            }),
          ],
        }),
      };
    }
    assert.equal("tools" in request.body, false);
    const tool_message = request.body.messages.find(
      (message) => message.role === "tool"
    );
    assert.equal(JSON.parse(tool_message.content).ok, true);
    return {
      body: completion({
        content: "The cube was created from the real ToolResult.",
      }),
    };
  });
  t.after(() => fake.close());
  const core = createTestCore();
  const client = provider(fake.base_url);
  const service = new AgentService({
    session_manager: core.session_manager,
    tool_registry: core.tool_registry,
    command_executor: core.command_executor,
    provider: client,
    audit_log: { write: async () => {} },
  });
  const result = await service.run({
    protocol_version: "1.0",
    request_id: randomUUID(),
    session_id: core.session.session_id,
    message: "create a cube",
    expected_revision: 0,
  });

  assert.equal(result.ok, true);
  assert.equal(result.assistant_message, "The cube was created from the real ToolResult.");
  assert.equal(core.world.revision, 1);
  assert.equal(core.world.entities.size, 1);
  assert.equal(request_count, 2);
});
