import { AgentError, asAgentError } from "../protocol/errors.mjs";
import {
  assertProtocolVersion,
  assertUuid,
} from "../protocol/envelope.mjs";

function validateProviderOutput(output) {
  if (
    !output ||
    typeof output !== "object" ||
    Array.isArray(output) ||
    typeof output.assistant_message !== "string" ||
    !Array.isArray(output.tool_calls)
  ) {
    throw new AgentError(
      "MODEL_OUTPUT_INVALID",
      "Provider output must contain assistant_message and tool_calls"
    );
  }
  for (const [index, tool_call] of output.tool_calls.entries()) {
    if (
      !tool_call ||
      typeof tool_call !== "object" ||
      Array.isArray(tool_call)
    ) {
      throw new AgentError(
        "MODEL_OUTPUT_INVALID",
        "Provider returned an invalid ToolCall",
        { tool_call_index: index }
      );
    }
    const valid_shape =
      typeof tool_call.tool_call_id === "string" &&
      typeof tool_call.tool_name === "string" &&
      tool_call.tool_name.length > 0 &&
      Number.isSafeInteger(tool_call.expected_revision) &&
      tool_call.expected_revision >= 0 &&
      typeof tool_call.dry_run === "boolean" &&
      tool_call.args &&
      typeof tool_call.args === "object" &&
      !Array.isArray(tool_call.args);
    if (!valid_shape) {
      throw new AgentError(
        "MODEL_OUTPUT_INVALID",
        "Provider ToolCall is missing required structured fields",
        { tool_call_index: index }
      );
    }
    try {
      assertUuid(tool_call.tool_call_id, `tool_calls[${index}].tool_call_id`);
    } catch {
      throw new AgentError(
        "MODEL_OUTPUT_INVALID",
        "Provider ToolCall tool_call_id must be a UUID",
        { tool_call_index: index }
      );
    }
  }
  return output;
}

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

  async run(request) {
    const started_at = Date.now();
    let session;
    let world;
    try {
      assertProtocolVersion(request.protocol_version);
      assertUuid(request.request_id, "request_id");
      assertUuid(request.session_id, "session_id");
      session = this.session_manager.getSession(request.session_id);
      world = session.world;
      if (
        request.world_id !== undefined &&
        request.world_id !== world.world_id
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
        world
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
      if (request.expected_revision !== world.revision) {
        throw new AgentError(
          "REVISION_CONFLICT",
          `Expected revision ${request.expected_revision}, current revision is ${world.revision}`,
          {
            expected_revision: request.expected_revision,
            current_revision: world.revision,
          },
          true
        );
      }
    } catch (error) {
      const result = this.#failure(
        request,
        asAgentError(error, "INVALID_REQUEST"),
        world
      );
      session.agent_request_results.set(
        request.request_id,
        structuredClone(result)
      );
      await this.#writeAgentOnlyAudit(request, result, [], started_at);
      return result;
    }

    const execution = this.#runProviderAndTools(request, world, started_at);
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

  async #runProviderAndTools(request, world, started_at) {
    let provider_output;
    try {
      provider_output = validateProviderOutput(
        await this.provider.run({
          message: request.message.trim(),
          world_snapshot: world.snapshot(),
          tool_definitions: this.tool_registry.listDefinitions(),
        })
      );
    } catch (error) {
      const agent_error =
        error instanceof AgentError
          ? error
          : new AgentError(
              "MODEL_OUTPUT_INVALID",
              error?.message || String(error)
            );
      const result = this.#failure(request, agent_error, world);
      await this.#writeAgentOnlyAudit(request, result, [], started_at);
      return result;
    }

    if (provider_output.tool_calls.length === 0) {
      const result = {
        ok: true,
        request_id: request.request_id,
        assistant_message: provider_output.assistant_message,
        tool_results: [],
        world_revision: world.revision,
        undo_token: null,
        error: null,
        replayed: false,
      };
      await this.#writeAgentOnlyAudit(request, result, [], started_at);
      return result;
    }

    const execution = await this.command_executor.executeBatch({
      protocol_version: request.protocol_version,
      request_id: request.request_id,
      session_id: request.session_id,
      world_id: world.world_id,
      tool_calls: provider_output.tool_calls,
      provider: this.provider.name,
      user_message: request.message.trim(),
    });
    return {
      ok: execution.ok,
      request_id: request.request_id,
      assistant_message: execution.ok
        ? provider_output.assistant_message
        : `操作未完成：${execution.error.message}`,
      tool_results: execution.tool_results,
      world_revision: execution.world_revision,
      undo_token: execution.undo_token,
      error: execution.error,
      replayed: execution.replayed,
    };
  }

  #failure(request, error, world) {
    return {
      ok: false,
      request_id: request?.request_id ?? null,
      assistant_message: `操作未完成：${error.message}`,
      tool_results: [],
      world_revision: world?.revision ?? null,
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
        "[CattyAgentBridge] audit log write failed:",
        error?.message || error
      );
    }
  }
}
