import { randomUUID } from "node:crypto";
import { AgentError, asAgentError } from "../protocol/errors.mjs";
import {
  assertProtocolVersion,
  assertUuid,
} from "../protocol/envelope.mjs";
import { NullAuditLog } from "../logging/audit-log.mjs";
import { applyToolResultsToEntityContext } from "../sessions/entity-context.mjs";
import {
  validateExecuteTransactionResult,
  validateUndoResult,
} from "../world/world-adapter-contract.mjs";
import {
  WorldAdapterError,
  worldAdapterErrorToAgentError,
} from "../world/world-adapter-errors.mjs";

const TOOL_CALL_FIELDS = new Set([
  "tool_call_id",
  "tool_name",
  "expected_revision",
  "dry_run",
  "args",
]);

function clone(value) {
  return structuredClone(value);
}

function validateToolCallShape(tool_call, index) {
  if (!tool_call || typeof tool_call !== "object" || Array.isArray(tool_call)) {
    throw new AgentError("INVALID_REQUEST", "ToolCall must be an object", {
      tool_call_index: index,
    });
  }
  for (const key of Object.keys(tool_call)) {
    if (!TOOL_CALL_FIELDS.has(key)) {
      throw new AgentError(
        "INVALID_REQUEST",
        `Unknown ToolCall field: ${key}`,
        { tool_call_index: index, field: key }
      );
    }
  }
  assertUuid(tool_call.tool_call_id, `tool_calls[${index}].tool_call_id`);
  if (typeof tool_call.tool_name !== "string" || !tool_call.tool_name) {
    throw new AgentError("INVALID_REQUEST", "tool_name is required", {
      tool_call_index: index,
    });
  }
  if (
    !Number.isSafeInteger(tool_call.expected_revision) ||
    tool_call.expected_revision < 0
  ) {
    throw new AgentError(
      "INVALID_REQUEST",
      "expected_revision must be a non-negative safe integer",
      { tool_call_index: index }
    );
  }
  if (typeof tool_call.dry_run !== "boolean") {
    throw new AgentError("INVALID_REQUEST", "dry_run must be a boolean", {
      tool_call_index: index,
    });
  }
  if (
    !tool_call.args ||
    typeof tool_call.args !== "object" ||
    Array.isArray(tool_call.args)
  ) {
    throw new AgentError("INVALID_REQUEST", "args must be an object", {
      tool_call_index: index,
    });
  }
}

function failureResult({
  request_id,
  tool_call_id = null,
  revision,
  error,
  dry_run = false,
}) {
  return {
    ok: false,
    request_id: request_id ?? null,
    tool_call_id,
    before_revision: revision ?? null,
    after_revision: revision ?? null,
    changes: [],
    undo_token: null,
    error: error.toJSON(),
    data: null,
    dry_run,
  };
}

function asExecutionError(error) {
  if (error instanceof WorldAdapterError) {
    return worldAdapterErrorToAgentError(error);
  }
  return asAgentError(error, "EXECUTION_FAILED");
}

export class CommandExecutor {
  constructor({
    session_manager,
    tool_registry,
    audit_log = new NullAuditLog(),
  }) {
    this.session_manager = session_manager;
    this.tool_registry = tool_registry;
    this.audit_log = audit_log;
  }

  async executeBatch(request) {
    const started_at = Date.now();
    let session;
    let adapter;

    try {
      assertProtocolVersion(request.protocol_version);
      assertUuid(request.request_id, "request_id");
      assertUuid(request.session_id, "session_id");
      assertUuid(request.world_id, "world_id");
      session = this.session_manager.getSession(request.session_id);
      adapter = this.session_manager.getAdapter(
        request.session_id,
        request.world_id
      );
    } catch (error) {
      const agent_error = asAgentError(error, "INVALID_REQUEST");
      const result = this.#failureResponse(request, null, agent_error);
      await this.#writeAudit(
        request,
        result,
        started_at,
        adapter,
        null
      );
      return result;
    }

    const cached = session.request_results.get(request.request_id);
    if (cached) {
      return {
        ...clone(cached),
        replayed: true,
      };
    }

    const pending = session.request_promises.get(request.request_id);
    if (pending) {
      const result = await pending;
      return {
        ...clone(result),
        replayed: true,
      };
    }

    if (session.busy) {
      const error = new AgentError(
        "BUSY",
        "Another transaction is already executing for this session",
        { session_id: request.session_id },
        true
      );
      const revision =
        session.last_world_revision ??
        request.tool_calls?.[0]?.expected_revision ??
        null;
      const result = this.#failureResponse(request, revision, error);
      session.request_results.set(request.request_id, clone(result));
      await this.#writeAudit(
        request,
        result,
        started_at,
        adapter,
        null
      );
      return result;
    }

    session.busy = true;
    const execution = this.#executeWithAdapter(
      request,
      session,
      adapter
    );
    session.request_promises.set(
      request.request_id,
      execution.then((outcome) => outcome.result)
    );
    try {
      const outcome = await execution;
      const result = outcome.result;
      if (result.ok && !result.replayed) {
        try {
          const snapshot = await adapter.getSnapshot({
            request_id: randomUUID(),
            session_id: request.session_id,
            world_id: request.world_id,
            signal: request.signal,
          });
          session.last_world_revision = snapshot.revision;
          applyToolResultsToEntityContext({
            session,
            snapshot,
            tool_calls: request.tool_calls,
            tool_results: result.tool_results,
          });
        } catch {
          // Execution remains authoritative. If post-execution snapshot
          // refresh fails, leave EntityContext unchanged rather than guessing.
        }
      } else if (Number.isSafeInteger(result.world_revision)) {
        session.last_world_revision = result.world_revision;
      }
      session.request_results.set(request.request_id, clone(result));
      await this.#writeAudit(
        request,
        result,
        started_at,
        adapter,
        outcome.adapter_metadata
      );
      return result;
    } finally {
      session.request_promises.delete(request.request_id);
      session.busy = false;
    }
  }

  async #executeWithAdapter(request, session, adapter) {
    let prepared;
    try {
      prepared = this.#prepareTransaction(request, adapter);
      if (prepared.undo) {
        if (prepared.dry_run) {
          throw new AgentError(
            "INVALID_ARGUMENT",
            "dry_run is not supported for history.undo"
          );
        }
        const adapter_request = {
          request_id: request.request_id,
          session_id: request.session_id,
          world_id: request.world_id,
          expected_revision: prepared.expected_revision,
          undo_token: prepared.tool_calls[0].args.undo_token ?? null,
          signal: request.signal,
        };
        const adapter_result = validateUndoResult(
          await adapter.undo(adapter_request),
          adapter_request
        );
        const tool_call = prepared.tool_calls[0];
        const tool_result = {
          ok: adapter_result.ok,
          request_id: request.request_id,
          tool_call_id: tool_call.tool_call_id,
          before_revision: adapter_result.before_revision,
          after_revision: adapter_result.after_revision,
          changes: clone(adapter_result.changes),
          undo_token: null,
          error: clone(adapter_result.error),
          data: clone(adapter_result.data ?? null),
          dry_run: false,
        };
        return {
          result: {
            ok: adapter_result.ok,
            request_id: request.request_id,
            tool_results: [tool_result],
            world_revision: adapter_result.after_revision,
            undo_token: null,
            error: clone(adapter_result.error),
            failed_tool_call_index: adapter_result.ok ? null : 0,
            replayed: adapter_result.replayed,
          },
          adapter_metadata: clone(
            adapter_result.adapter_metadata ?? null
          ),
        };
      }

      const adapter_request = {
        request_id: request.request_id,
        session_id: request.session_id,
        world_id: request.world_id,
        expected_revision: prepared.expected_revision,
        dry_run: prepared.dry_run,
        atomic: true,
        tool_calls: prepared.tool_calls.map((tool_call) => ({
          tool_call_id: tool_call.tool_call_id,
          tool_name: tool_call.tool_name,
          args: clone(tool_call.args),
        })),
        signal: request.signal,
      };
      const adapter_result = validateExecuteTransactionResult(
        await adapter.executeTransaction(adapter_request),
        adapter_request
      );
      return {
        result: {
          ok: adapter_result.ok,
          request_id: request.request_id,
          tool_results: clone(adapter_result.tool_results),
          world_revision: adapter_result.after_revision,
          undo_token: adapter_result.undo_token,
          error: clone(adapter_result.error),
          failed_tool_call_index:
            adapter_result.failed_tool_call_index ?? null,
          replayed: adapter_result.replayed,
        },
        adapter_metadata: clone(
          adapter_result.adapter_metadata ?? null
        ),
      };
    } catch (error) {
      const agent_error = asExecutionError(error);
      const revision =
        session.last_world_revision ??
        prepared?.expected_revision ??
        request.tool_calls?.[0]?.expected_revision ??
        null;
      return {
        result: this.#failureResponse(request, revision, agent_error),
        adapter_metadata:
          error instanceof WorldAdapterError
            ? {
                adapter_request_phase: error.phase,
                adapter_http_status: error.http_status,
                adapter_timeout: error.timeout,
                adapter_cancelled: error.cancelled,
                remote_error_class: error.reason,
              }
            : null,
      };
    }
  }

  #prepareTransaction(request, adapter) {
    const max_tool_calls = Math.min(
      100,
      adapter.capabilities.max_tool_calls
    );
    if (
      !Array.isArray(request.tool_calls) ||
      request.tool_calls.length === 0 ||
      request.tool_calls.length > max_tool_calls
    ) {
      throw new AgentError(
        "INVALID_REQUEST",
        `tool_calls must contain between 1 and ${max_tool_calls} calls`,
        { field: "tool_calls", max_tool_calls }
      );
    }

    const validated = [];
    for (let index = 0; index < request.tool_calls.length; index += 1) {
      const tool_call = request.tool_calls[index];
      validateToolCallShape(tool_call, index);
      const tool = this.tool_registry.get(tool_call.tool_name);
      const args = this.tool_registry.validate(
        tool_call.tool_name,
        tool_call.args
      );
      validated.push({
        ...clone(tool_call),
        args: clone(args),
        tool,
      });
    }

    const expected_revisions = new Set(
      validated.map((tool_call) => tool_call.expected_revision)
    );
    if (expected_revisions.size !== 1) {
      throw new AgentError(
        "INVALID_ARGUMENT",
        "All tool_calls in one transaction must use the same expected_revision",
        { field: "tool_calls[].expected_revision" }
      );
    }
    const dry_run_values = new Set(
      validated.map((tool_call) => tool_call.dry_run)
    );
    if (dry_run_values.size !== 1) {
      throw new AgentError(
        "INVALID_ARGUMENT",
        "All tool_calls in one transaction must use the same dry_run value",
        { field: "tool_calls[].dry_run" }
      );
    }
    const contains_undo = validated.some(
      (tool_call) => tool_call.tool.name === "history.undo"
    );
    if (contains_undo && validated.length !== 1) {
      throw new AgentError(
        "INVALID_ARGUMENT",
        "history.undo must be executed as a single-call transaction"
      );
    }

    return {
      tool_calls: validated,
      expected_revision: validated[0].expected_revision,
      dry_run: validated[0].dry_run,
      undo: contains_undo,
    };
  }

  #failureResponse(request, revision, error) {
    return {
      ok: false,
      request_id: request?.request_id ?? null,
      tool_results: [
        failureResult({
          request_id: request?.request_id,
          tool_call_id:
            request?.tool_calls?.[0]?.tool_call_id ?? null,
          revision,
          error,
          dry_run: request?.tool_calls?.[0]?.dry_run ?? false,
        }),
      ],
      world_revision: revision,
      undo_token: null,
      error: error.toJSON(),
      failed_tool_call_index: null,
      replayed: false,
    };
  }

  async #writeAudit(
    request,
    result,
    started_at,
    adapter,
    adapter_metadata
  ) {
    try {
      await this.audit_log.write({
        request_id: request?.request_id,
        session_id: request?.session_id,
        provider: request?.provider || "direct",
        user_message: request?.user_message ?? null,
        world_adapter: adapter?.name ?? null,
        adapter_protocol_version:
          adapter?.protocolVersion ?? null,
        adapter_request_phase:
          adapter_metadata?.adapter_request_phase ?? "execute",
        adapter_duration_ms:
          adapter_metadata?.adapter_duration_ms ??
          Date.now() - started_at,
        adapter_http_status:
          adapter_metadata?.adapter_http_status ?? null,
        adapter_replayed:
          adapter_metadata?.adapter_replayed ?? result.replayed ?? false,
        adapter_timeout:
          adapter_metadata?.adapter_timeout ??
          result.error?.code === "TIMEOUT",
        adapter_cancelled:
          adapter_metadata?.adapter_cancelled ?? false,
        remote_error_class:
          adapter_metadata?.remote_error_class ??
          result.error?.details?.adapter_reason ??
          null,
        before_revision:
          result.tool_results?.[0]?.before_revision ??
          result.world_revision,
        after_revision: result.world_revision,
        tool_calls: request?.tool_calls || [],
        tool_results: result.tool_results,
        error: result.error,
        duration_ms: Date.now() - started_at,
        failed_tool_call_index:
          result.failed_tool_call_index ?? null,
      });
    } catch (error) {
      console.error(
        "[MahoAgentBridge] audit log write failed:",
        error?.message || error
      );
    }
  }
}
