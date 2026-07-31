import assert from "node:assert/strict";
import test from "node:test";
import { main as runDeepSeekEval } from "../evals/run-deepseek-evals.mjs";
import { main as runDeepSeekSmoke } from "../evals/smoke-deepseek.mjs";
import { hasDeepSeekKey } from "../evals/real-provider-core.mjs";
import {
  completion,
  providerToolCall,
  startFakeOpenAIServer,
} from "./helpers/fake-openai-server.mjs";

function sink() {
  let text = "";
  return {
    write(value) {
      text += String(value);
      return true;
    },
    text() {
      return text;
    },
  };
}

test("real entrypoint key detection trims values and honors fallback order", () => {
  assert.equal(hasDeepSeekKey({}), false);
  assert.equal(
    hasDeepSeekKey({
      MAHO_AI_API_KEY: "   ",
      DEEPSEEK_API_KEY: "fallback-key",
    }),
    true
  );
});

test("real DeepSeek eval exits clearly without a key and makes no request", async () => {
  const output = sink();
  const error_output = sink();
  const exit_code = await runDeepSeekEval({
    env: {},
    output,
    error_output,
  });
  assert.equal(exit_code, 1);
  assert.match(error_output.text(), /requires/);
  assert.match(error_output.text(), /No network request/);
});

test("real DeepSeek smoke exits clearly without a key and makes no request", async () => {
  const output = sink();
  const error_output = sink();
  const exit_code = await runDeepSeekSmoke({
    env: {},
    output,
    error_output,
  });
  assert.equal(exit_code, 1);
  assert.match(error_output.text(), /requires/);
  assert.match(error_output.text(), /no network request/i);
});

test("DeepSeek smoke uses text-only request then ToolCall and finalization", async (t) => {
  let request_count = 0;
  const fake = await startFakeOpenAIServer((request) => {
    request_count += 1;
    const usage = {
      prompt_tokens: 5,
      completion_tokens: 2,
      total_tokens: 7,
    };
    if (request_count === 1) {
      assert.equal("tools" in request.body, false);
      return { body: completion({ content: "online", usage }) };
    }
    if (request_count === 2) {
      assert.equal(request.body.tools.length, 8);
      return {
        body: completion({
          content: "",
          finish_reason: "tool_calls",
          usage,
          tool_calls: [
            providerToolCall({
              name: "entity__spawn_primitive",
              arguments:
                '{"primitive_type":"cube","properties":{"color":[1,0,0,1]}}',
            }),
          ],
        }),
      };
    }
    assert.equal("tools" in request.body, false);
    return {
      body: completion({
        content: "created from ToolResult",
        usage,
      }),
    };
  });
  t.after(() => fake.close());
  const output = sink();
  const error_output = sink();
  const exit_code = await runDeepSeekSmoke({
    env: {
      MAHO_AI_BASE_URL: fake.base_url,
      MAHO_AI_MODEL: "deepseek-test-model",
      DEEPSEEK_API_KEY: "test-key",
    },
    output,
    error_output,
  });

  assert.equal(exit_code, 0, error_output.text());
  assert.equal(request_count, 3);
  assert.match(output.text(), /smoke PASS/);
  assert.equal(output.text().includes("test-key"), false);
});
