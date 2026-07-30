import { pathToFileURL } from "node:url";
import { randomUUID } from "node:crypto";
import { createPlanMessages } from "../src/agent/normalized-messages.mjs";
import {
  createRealProviderCore,
  hasDeepSeekKey,
} from "./real-provider-core.mjs";

export async function main({
  env = process.env,
  output = process.stdout,
  error_output = process.stderr,
} = {}) {
  if (!hasDeepSeekKey(env)) {
    error_output.write(
      "DeepSeek smoke test requires CATTY_AI_API_KEY or DEEPSEEK_API_KEY. Set one in the process environment; no network request was made.\n"
    );
    return 1;
  }
  const started_at = performance.now();
  let core;
  try {
    core = await createRealProviderCore({ env });
    const metadata = core.provider.getMetadata();
    const text_session = core.createSession();
    const text_message =
      "Reply briefly that the Catty DeepSeek smoke test is online.";
    const text = await core.provider.finalize({
      request_id: randomUUID(),
      session_id: text_session.session_id,
      user_message: text_message,
      normalized_messages: createPlanMessages({
        system_message:
          "Return a short plain-text response. No tools are available.",
        user_message: text_message,
      }),
      world_snapshot: text_session.world.snapshot(),
      session_context: structuredClone(text_session.entity_context),
      tool_definitions: core.tool_registry.listDefinitions(),
    });
    if (!text.assistant_message.trim() || text.tool_calls.length !== 0) {
      throw new Error("plain response check failed");
    }

    const tool_session = core.createSession();
    const tool = await core.run(tool_session, "创建一个红色立方体");
    if (
      !tool.result.ok ||
      !tool.plan_tool_names.includes("entity.spawn_primitive") ||
      tool_session.world.entities.size !== 1
    ) {
      throw new Error("ToolCall or MockWorld check failed");
    }
    const finalization_record = tool.records.find(
      (record) => record.request_phase === "finalize"
    );
    if (
      !finalization_record ||
      finalization_record.finalization_failed
    ) {
      throw new Error("finalization check failed");
    }
    const usage = core.audit_log.usage();
    if (usage.total_tokens === null) {
      throw new Error("usage check failed");
    }
    output.write(
      `DeepSeek smoke PASS provider=${metadata.provider} model=${metadata.model} auth=ok text=ok tool=ok finalization=ok usage=${JSON.stringify(usage)} timeout_ms=${core.config.timeout_ms} duration_ms=${Math.round(performance.now() - started_at)}\n`
    );
    return 0;
  } catch (error) {
    error_output.write(
      `DeepSeek smoke FAIL: ${error?.message || String(error)}\n`
    );
    return 1;
  } finally {
    await core?.close();
  }
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  process.exitCode = await main();
}
