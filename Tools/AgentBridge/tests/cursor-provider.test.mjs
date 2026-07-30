import assert from "node:assert/strict";
import test from "node:test";
import { CursorProvider } from "../src/agent/providers/cursor-provider.mjs";
import { MockWorld } from "../src/world/mock-world.mjs";
import { createDefaultToolRegistry } from "../src/tools/definitions.mjs";

test("CursorProvider converts SDK customTools callbacks into internal ToolCalls", async () => {
  let send_options;
  const fake_agent = {
    async send(_prompt, options) {
      send_options = options;
      await options.local.customTools["entity.spawn_primitive"].execute({
        primitive_type: "cube",
      });
      return {
        async *stream() {
          yield {
            type: "assistant",
            message: {
              content: [{ type: "text", text: "Planned one cube." }],
            },
          };
        },
        async wait() {},
      };
    },
  };
  const world = new MockWorld();
  const provider = new CursorProvider({ agent: fake_agent });
  const output = await provider.run({
    message: "spawn a cube",
    world_snapshot: world.snapshot(),
    tool_definitions: createDefaultToolRegistry().listDefinitions(),
  });

  assert.equal(send_options.mode, "plan");
  assert.equal(output.assistant_message, "Planned one cube.");
  assert.equal(output.tool_calls.length, 1);
  assert.equal(output.tool_calls[0].tool_name, "entity.spawn_primitive");
  assert.equal(output.tool_calls[0].expected_revision, 0);
  assert.match(output.tool_calls[0].tool_call_id, /^[0-9a-f-]{36}$/);
});

