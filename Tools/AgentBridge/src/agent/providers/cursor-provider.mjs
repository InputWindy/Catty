import { randomUUID } from "node:crypto";
import { AgentError } from "../../protocol/errors.mjs";
import { buildAgentPrompt } from "../prompt-builder.mjs";

export class CursorProvider {
  constructor({ agent }) {
    this.name = "cursor";
    this.agent = agent;
  }

  async run({ message, world_snapshot, tool_definitions }) {
    if (!this.agent) {
      throw new AgentError(
        "EXECUTION_FAILED",
        "CursorProvider has no initialized Cursor Agent"
      );
    }

    const captured_tool_calls = [];
    const custom_tools = Object.fromEntries(
      tool_definitions.map((definition) => [
        definition.name,
        {
          description: definition.description,
          inputSchema: definition.schema,
          execute: (args) => {
            captured_tool_calls.push({
              tool_call_id: randomUUID(),
              tool_name: definition.name,
              expected_revision: world_snapshot.revision,
              dry_run: false,
              args,
            });
            return {
              queued: true,
              executed: false,
              message:
                "Queued for Agent Core validation and execution after the model run.",
            };
          },
        },
      ])
    );

    const prompt = buildAgentPrompt({
      message,
      world_snapshot,
      tool_definitions,
    });
    let assistant_message = "";
    const run = await this.agent.send(prompt, {
      mode: "plan",
      local: { customTools: custom_tools },
    });
    for await (const event of run.stream()) {
      if (event.type === "assistant" && event.message?.content) {
        for (const block of event.message.content) {
          if (block.type === "text" && block.text) {
            assistant_message += block.text;
          }
        }
      }
    }
    await run.wait();

    if (!assistant_message.trim()) {
      assistant_message = captured_tool_calls.length
        ? "已生成结构化工具调用，等待 Agent Core 执行结果。"
        : "CursorProvider 未返回文本或工具调用。";
    }
    return {
      assistant_message: assistant_message.trim(),
      tool_calls: captured_tool_calls,
    };
  }
}
