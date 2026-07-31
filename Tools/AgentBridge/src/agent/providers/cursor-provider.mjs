import { randomUUID } from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { AgentError } from "../../protocol/errors.mjs";
import {
  DEFAULT_PROVIDER_CAPABILITIES,
  createProviderOutput,
} from "../provider-contract.mjs";
import {
  ProviderError,
  providerErrorReasons,
} from "./provider-errors.mjs";
import { buildAgentPrompt } from "../prompt-builder.mjs";

export class CursorProvider {
  constructor({
    agent,
    model = "composer-2.5",
    owns_agent = false,
  }) {
    this.name = "cursor";
    this.model = model;
    this.agent = agent;
    this.owns_agent = owns_agent;
    this.capabilities = Object.freeze({
      ...DEFAULT_PROVIDER_CAPABILITIES,
      supports_finalization: false,
    });
  }

  static async create(config) {
    if (!config.api_key) {
      throw new ProviderError(
        providerErrorReasons.API_KEY_MISSING,
        "CURSOR_API_KEY is required for Provider cursor",
        { provider: "cursor", model: config.model }
      );
    }
    const { Agent, JsonlLocalAgentStore } = await import("@cursor/sdk");
    const store_root = path.join(
      config.cwd,
      "Saved",
      "Agent",
      "cursor-sdk-store"
    );
    fs.mkdirSync(store_root, { recursive: true });
    const store = new JsonlLocalAgentStore(store_root);
    const agent = await Agent.create({
      apiKey: config.api_key,
      model: { id: config.model },
      local: { cwd: config.cwd, store },
    });
    return new CursorProvider({
      agent,
      model: config.model,
      owns_agent: true,
    });
  }

  getMetadata() {
    return {
      provider: this.name,
      model: this.model,
      ready: Boolean(this.agent),
      real: true,
      thinking: "disabled",
      capabilities: { ...this.capabilities },
    };
  }

  getAgent() {
    return this.agent;
  }

  async plan(input) {
    const output = await this.run({
      message: input.user_message,
      world_snapshot: input.world_snapshot,
      tool_definitions: input.tool_definitions,
    });
    return createProviderOutput({
      provider: this.name,
      model: this.model,
      assistant_message: output.assistant_message,
      tool_calls: output.tool_calls.map((tool_call) => ({
        tool_call_id: tool_call.tool_call_id,
        tool_name: tool_call.tool_name,
        args: tool_call.args,
      })),
      finish_reason: output.tool_calls.length ? "tool_calls" : "stop",
      provider_metadata: {
        phase: "plan",
        attempt_count: 1,
        duration_ms: 0,
        http_status: null,
      },
    });
  }

  async close() {
    if (!this.owns_agent || !this.agent) {
      return;
    }
    if (typeof this.agent[Symbol.asyncDispose] === "function") {
      await this.agent[Symbol.asyncDispose]();
    } else if (typeof this.agent.close === "function") {
      await this.agent.close();
    }
    this.agent = null;
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
