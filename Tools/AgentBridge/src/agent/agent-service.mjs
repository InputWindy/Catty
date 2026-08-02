import { randomUUID } from "node:crypto";
import { AgentError, asAgentError } from "../protocol/errors.mjs";
import {
  assertProtocolVersion,
  assertUuid,
} from "../protocol/envelope.mjs";
import {
  createProviderOutput,
  validateProviderOutput,
} from "./provider-contract.mjs";
import {
  createAssistantToolMessage,
  createPlanMessages,
  createToolResultMessage,
} from "./normalized-messages.mjs";
import { buildProviderSystemPrompt } from "./prompt-builder.mjs";
import {
  ProviderError,
  providerErrorToAgentError,
} from "./providers/provider-errors.mjs";
import {
  WorldAdapterError,
  worldAdapterErrorToAgentError,
} from "../world/world-adapter-errors.mjs";

const SESSION_MESSAGE_LIMIT = 40;

export class AgentService {
  constructor({
    session_manager,
    tool_registry,
    command_executor,
    provider,
    audit_log,
  }) {
    this.session_manager = session_manager;
    this.tool_registry = tool_registry;
    this.command_executor = command_executor;
    this.provider = provider;
    this.audit_log = audit_log;
  }

  getProviderMetadata() {
    if (typeof this.provider.getMetadata === "function") {
      return structuredClone(this.provider.getMetadata());
    }
    return {
      provider: this.provider.name || "legacy",
      model: this.provider.model || "legacy",
      ready: true,
    };
  }

  async run(request) {
    const started_at = Date.now();
    let session;
    let adapter;
    try {
      assertProtocolVersion(request.protocol_version);
      assertUuid(request.request_id, "request_id");
      assertUuid(request.session_id, "session_id");
      session = this.session_manager.getSession(request.session_id);
      adapter = session.adapter;
      if (
        request.world_id !== undefined &&
        request.world_id !== session.world_id
      ) {
        throw new AgentError(
          "UNKNOWN_WORLD",
          `Unknown world: ${request.world_id}`,
          { session_id: request.session_id, world_id: request.world_id }
        );
      }
    } catch (error) {
      const result = this.#failure(
        request,
        asAgentError(error, "INVALID_REQUEST"),
        session?.last_world_revision ?? null
      );
      await this.#writeAgentOnlyAudit(request, result, [], started_at);
      return result;
    }

    const cached = session.agent_request_results.get(request.request_id);
    if (cached) {
      return { ...structuredClone(cached), replayed: true };
    }
    const pending = session.agent_request_promises.get(request.request_id);
    if (pending) {
      const result = await pending;
      return { ...structuredClone(result), replayed: true };
    }

    let world_snapshot;
    try {
      if (typeof request.message !== "string" || !request.message.trim()) {
        throw new AgentError("INVALID_REQUEST", "message is required", {
          field: "message",
        });
      }
      if (
        !Number.isSafeInteger(request.expected_revision) ||
        request.expected_revision < 0
      ) {
        throw new AgentError(
          "INVALID_REQUEST",
          "expected_revision must be a non-negative safe integer",
          { field: "expected_revision" }
        );
      }
      world_snapshot = await adapter.getSnapshot({
        request_id: randomUUID(),
        session_id: request.session_id,
        world_id: session.world_id,
        signal: request.signal,
      });
      session.last_world_revision = world_snapshot.revision;
      if (request.expected_revision !== world_snapshot.revision) {
        throw new AgentError(
          "REVISION_CONFLICT",
          `Expected revision ${request.expected_revision}, current revision is ${world_snapshot.revision}`,
          {
            expected_revision: request.expected_revision,
            current_revision: world_snapshot.revision,
          },
          true
        );
      }
    } catch (error) {
      const result = this.#failure(
        request,
        this.#worldAgentError(error, "INVALID_REQUEST"),
        world_snapshot?.revision ?? session.last_world_revision
      );
      session.agent_request_results.set(
        request.request_id,
        structuredClone(result)
      );
      await this.#writeAgentOnlyAudit(request, result, [], started_at);
      return result;
    }

    const execution = this.#runProviderAndTools(
      request,
      session,
      adapter,
      world_snapshot,
      started_at
    );
    session.agent_request_promises.set(request.request_id, execution);
    try {
      const result = await execution;
      session.agent_request_results.set(
        request.request_id,
        structuredClone(result)
      );
      return result;
    } finally {
      session.agent_request_promises.delete(request.request_id);
    }
  }

  async #runProviderAndTools(
    request,
    session,
    adapter,
    world_snapshot,
    started_at
  ) {
    let provider_output;
    const supported_tools = new Set(
      adapter.capabilities.supported_tools
    );
    const tool_definitions = this.tool_registry
      .listDefinitions()
      .filter((definition) => supported_tools.has(definition.name));
    const normalized_messages = createPlanMessages({
      system_message: buildProviderSystemPrompt({
        world_snapshot,
        tool_definitions,
        session_context: session.entity_context,
      }),
      session_messages: session.normalized_messages,
      user_message: request.message.trim(),
    });
    const provider_input = {
      request_id: request.request_id,
      session_id: request.session_id,
      user_message: request.message.trim(),
      normalized_messages,
      world_snapshot,
      session_context: structuredClone(session.entity_context),
      tool_definitions,
      signal: request.signal,
    };
    try {
      provider_output = await this.#plan(provider_input);
      await this.#writeProviderAudit({
        request,
        output: provider_output,
        phase: "plan",
        finalization_used: false,
        finalization_failed: false,
      });
    } catch (error) {
      const agent_error = this.#providerAgentError(error);
      const result = this.#failure(
        request,
        agent_error,
        world_snapshot.revision
      );
      await this.#writeProviderAudit({
        request,
        error,
        phase: "plan",
        finalization_used: false,
        finalization_failed: false,
      });
      return result;
    }

    if (provider_output.tool_calls.length === 0) {
      const result = {
        ok: true,
        request_id: request.request_id,
        assistant_message: provider_output.assistant_message,
        tool_results: [],
        world_revision: world_snapshot.revision,
        undo_token: null,
        error: null,
        replayed: false,
      };
      this.#appendSessionMessages(session, [
        { role: "user", content: request.message.trim() },
        {
          role: "assistant",
          content: provider_output.assistant_message,
          tool_calls: [],
        },
      ]);
      await this.#writeAgentOnlyAudit(request, result, [], started_at);
      return result;
    }

    const executable_tool_calls = provider_output.tool_calls.map(
      (tool_call) => ({
        ...structuredClone(tool_call),
        expected_revision: world_snapshot.revision,
        dry_run: false,
      })
    );
    const execution = await this.command_executor.executeBatch({
      protocol_version: request.protocol_version,
      request_id: request.request_id,
      session_id: request.session_id,
      world_id: session.world_id,
      tool_calls: executable_tool_calls,
      provider: this.provider.name,
      user_message: request.message.trim(),
      signal: request.signal,
    });

    const assistant_tool_message =
      createAssistantToolMessage(provider_output);
    const tool_messages = provider_output.tool_calls.map(
      (tool_call, index) =>
        createToolResultMessage(
          tool_call,
          execution.tool_results[index] || {
            ok: false,
            error: execution.error,
          }
        )
    );
    const finalization_messages = [
      ...normalized_messages,
      assistant_tool_message,
      ...tool_messages,
    ];
    const finalization_used = Boolean(
      this.provider.capabilities?.supports_finalization &&
      this.provider.finalization_enabled !== false &&
      typeof this.provider.finalize === "function"
    );
    let finalization_output = null;
    let finalization_failed = false;
    if (finalization_used) {
      try {
        let finalization_snapshot = world_snapshot;
        if (execution.ok) {
          try {
            finalization_snapshot = await adapter.getSnapshot({
              request_id: randomUUID(),
              session_id: request.session_id,
              world_id: session.world_id,
              signal: request.signal,
            });
            session.last_world_revision =
              finalization_snapshot.revision;
          } catch {
            // ToolResults remain authoritative if the follow-up snapshot
            // cannot be refreshed for optional finalization.
          }
        }
        finalization_output = validateProviderOutput(
          await this.provider.finalize({
            ...provider_input,
            normalized_messages: finalization_messages,
            world_snapshot: finalization_snapshot,
            session_context: structuredClone(session.entity_context),
          }),
          {
            expected_provider: this.provider.name,
            expected_model: this.provider.model,
            max_tool_calls: 0,
            phase: "finalize",
          }
        );
        await this.#writeProviderAudit({
          request,
          output: finalization_output,
          phase: "finalize",
          finalization_used: true,
          finalization_failed: false,
        });
      } catch (error) {
        finalization_failed = true;
        await this.#writeProviderAudit({
          request,
          error,
          phase: "finalize",
          finalization_used: true,
          finalization_failed: true,
        });
      }
    }

    const assistant_message = execution.ok
      ? finalization_output?.assistant_message?.trim() ||
        (finalization_failed
          ? this.#deterministicExecutionReply(execution)
          : provider_output.assistant_message)
      : `操作未完成：${execution.error.message}`;
    const result = {
      ok: execution.ok,
      request_id: request.request_id,
      assistant_message,
      tool_results: execution.tool_results,
      world_revision: execution.world_revision,
      undo_token: execution.undo_token,
      error: execution.error,
      replayed: execution.replayed,
    };
    this.#appendSessionMessages(session, [
      { role: "user", content: request.message.trim() },
      assistant_tool_message,
      ...tool_messages,
      {
        role: "assistant",
        content: assistant_message,
        tool_calls: [],
      },
    ]);
    return result;
  }

  async #plan(input) {
    let raw_output;
    if (typeof this.provider.plan === "function") {
      raw_output = await this.provider.plan(input);
    } else if (typeof this.provider.run === "function") {
      const legacy = await this.provider.run({
        message: input.user_message,
        world_snapshot: input.world_snapshot,
        tool_definitions: input.tool_definitions,
        session_context: input.session_context,
      });
      raw_output = createProviderOutput({
        provider: this.provider.name || "legacy",
        model: this.provider.model || "legacy",
        assistant_message: legacy?.assistant_message,
        tool_calls: Array.isArray(legacy?.tool_calls)
          ? legacy.tool_calls.map((tool_call) => ({
              tool_call_id: tool_call.tool_call_id,
              tool_name: tool_call.tool_name,
              args: tool_call.args,
            }))
          : legacy?.tool_calls,
        finish_reason: legacy?.tool_calls?.length ? "tool_calls" : "stop",
        provider_metadata: {
          phase: "plan",
          attempt_count: 1,
          duration_ms: 0,
          http_status: null,
        },
      });
    } else {
      throw new AgentError(
        "MODEL_OUTPUT_INVALID",
        "Selected Provider does not implement plan()"
      );
    }
    return validateProviderOutput(raw_output, {
      expected_provider: this.provider.name,
      expected_model: this.provider.model,
      max_tool_calls: this.provider.max_tool_calls ?? 16,
      phase: "plan",
    });
  }

  #providerAgentError(error) {
    if (error instanceof AgentError) {
      return error;
    }
    if (error instanceof ProviderError) {
      return providerErrorToAgentError(error);
    }
    return new AgentError(
      "MODEL_OUTPUT_INVALID",
      error?.message || String(error)
    );
  }

  #worldAgentError(error, fallback_code = "EXECUTION_FAILED") {
    if (error instanceof WorldAdapterError) {
      return worldAdapterErrorToAgentError(error);
    }
    return asAgentError(error, fallback_code);
  }

  #deterministicExecutionReply(execution) {
    if (!execution.ok) {
      return `操作未完成：${execution.error.message}`;
    }
    const successful_count = execution.tool_results.filter(
      (tool_result) => tool_result.ok
    ).length;
    return `操作已完成：${successful_count} 个工具调用已由 CommandExecutor 验证成功。`;
  }

  #appendSessionMessages(session, messages) {
    session.normalized_messages.push(...structuredClone(messages));
    if (session.normalized_messages.length > SESSION_MESSAGE_LIMIT) {
      session.normalized_messages.splice(
        0,
        session.normalized_messages.length - SESSION_MESSAGE_LIMIT
      );
    }
  }

  #failure(request, error, revision) {
    return {
      ok: false,
      request_id: request?.request_id ?? null,
      assistant_message: `操作未完成：${error.message}`,
      tool_results: [],
      world_revision: revision ?? null,
      undo_token: null,
      error: error.toJSON(),
      replayed: false,
    };
  }

  async #writeAgentOnlyAudit(request, result, tool_calls, started_at) {
    try {
      await this.audit_log.write({
        request_id: request.request_id,
        session_id: request.session_id,
        provider: this.provider.name,
        model: this.provider.model || null,
        request_phase: "agent",
        user_message: request.message,
        before_revision: result.world_revision,
        after_revision: result.world_revision,
        tool_calls,
        tool_results: result.tool_results,
        error: result.error,
        duration_ms: Date.now() - started_at,
      });
    } catch (error) {
      console.error(
        "[MahoAgentBridge] audit log write failed:",
        error?.message || error
      );
    }
  }

  async #writeProviderAudit({
    request,
    output = null,
    error = null,
    phase,
    finalization_used,
    finalization_failed,
  }) {
    const metadata = output?.provider_metadata || {};
    const provider_error =
      error instanceof ProviderError ? error : null;
    try {
      await this.audit_log.write({
        request_id: request.request_id,
        session_id: request.session_id,
        provider: output?.provider || this.provider.name,
        model: output?.model || this.provider.model || null,
        request_phase: phase,
        attempt_count:
          metadata.attempt_count ?? provider_error?.attempt_count ?? 0,
        duration_ms: metadata.duration_ms ?? 0,
        http_status:
          metadata.http_status ?? provider_error?.http_status ?? null,
        finish_reason: output?.finish_reason ?? null,
        tool_call_count: output?.tool_calls?.length ?? 0,
        input_tokens: output?.usage?.input_tokens ?? null,
        output_tokens: output?.usage?.output_tokens ?? null,
        total_tokens: output?.usage?.total_tokens ?? null,
        cached_input_tokens:
          output?.usage?.cached_input_tokens ?? null,
        finalization_used,
        finalization_failed,
        timeout: metadata.timeout ?? provider_error?.timeout ?? false,
        cancelled:
          metadata.cancelled ?? provider_error?.cancelled ?? false,
        user_message: request.message,
        before_revision: null,
        after_revision: null,
        tool_calls: output?.tool_calls || [],
        tool_results: [],
        error:
          error instanceof AgentError
            ? error.toJSON()
            : provider_error
              ? providerErrorToAgentError(provider_error).toJSON()
              : error
                ? { message: error?.message || String(error) }
                : null,
      });
    } catch (audit_error) {
      console.error(
        "[MahoAgentBridge] audit log write failed:",
        audit_error?.message || audit_error
      );
    }
  }
}
