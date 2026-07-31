import { AgentError, asAgentError } from "../protocol/errors.mjs";
import {
  assertProtocolVersion,
  assertUuid,
} from "../protocol/envelope.mjs";
import { NullAuditLog } from "../logging/audit-log.mjs";
import { applyToolResultsToEntityContext } from "../sessions/entity-context.mjs";

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
  };
}

export class CommandExecutor {
  constructor({
    session_manager,
    tool_registry,
    undo_journal,
    audit_log = new NullAuditLog(),
  }) {
    this.session_manager = session_manager;
    this.tool_registry = tool_registry;
    this.undo_journal = undo_journal;
    this.audit_log = audit_log;
  }

  async executeBatch(request) {
    const started_at = Date.now();
    let session;
    let world;

    try {
      assertProtocolVersion(request.protocol_version);
      assertUuid(request.request_id, "request_id");
      assertUuid(request.session_id, "session_id");
      assertUuid(request.world_id, "world_id");
      session = this.session_manager.getSession(request.session_id);
      world = this.session_manager.getWorld(request.session_id, request.world_id);
    } catch (error) {
      const agent_error = asAgentError(error, "INVALID_REQUEST");
      const result = this.#failureResponse(request, null, agent_error);
      await this.#writeAudit(request, result, started_at);
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
      const result = this.#failureResponse(request, world.revision, error);
      session.request_results.set(request.request_id, clone(result));
      await this.#writeAudit(request, result, started_at);
      return result;
    }

    session.busy = true;
    const execution = this.#executeTransaction(request, world);
    session.request_promises.set(request.request_id, execution);
    try {
      const result = await execution;
      if (result.ok) {
        applyToolResultsToEntityContext({
          session,
          tool_calls: request.tool_calls,
          tool_results: result.tool_results,
        });
      }
      session.request_results.set(request.request_id, clone(result));
      await this.#writeAudit(request, result, started_at);
      return result;
    } finally {
      session.request_promises.delete(request.request_id);
      session.busy = false;
    }
  }

  async #executeTransaction(request, world) {
    const before_revision = world.revision;
    let world_state;
    let journal_state;
    let failed_tool_call_index = null;
    const executed = [];

    try {
      if (
        !Array.isArray(request.tool_calls) ||
        request.tool_calls.length === 0 ||
        request.tool_calls.length > 100
      ) {
        throw new AgentError(
          "INVALID_REQUEST",
          "tool_calls must contain between 1 and 100 calls",
          { field: "tool_calls" }
        );
      }

      const validated = [];
      for (let index = 0; index < request.tool_calls.length; index += 1) {
        failed_tool_call_index = index;
        const tool_call = request.tool_calls[index];
        validateToolCallShape(tool_call, index);
        if (tool_call.expected_revision !== before_revision) {
          throw new AgentError(
            "REVISION_CONFLICT",
            `Expected revision ${tool_call.expected_revision}, current revision is ${before_revision}`,
            {
              tool_call_index: index,
              expected_revision: tool_call.expected_revision,
              current_revision: before_revision,
            },
            true
          );
        }
        const tool = this.tool_registry.get(tool_call.tool_name);
        const args = this.tool_registry.validate(tool_call.tool_name, tool_call.args);
        validated.push({ tool_call, tool, args });
      }

      const dry_run_values = new Set(
        validated.map(({ tool_call }) => tool_call.dry_run)
      );
      if (dry_run_values.size !== 1) {
        throw new AgentError(
          "INVALID_ARGUMENT",
          "All tool_calls in one transaction must use the same dry_run value",
          { field: "tool_calls[].dry_run" }
        );
      }
      if (
        validated.length > 1 &&
        validated.some(({ tool }) => tool.name === "history.undo")
      ) {
        throw new AgentError(
          "INVALID_ARGUMENT",
          "history.undo must be executed as a single-call transaction"
        );
      }

      const dry_run = validated[0].tool_call.dry_run;
      world_state = world.captureState();
      journal_state = this.undo_journal.captureState();

      for (let index = 0; index < validated.length; index += 1) {
        failed_tool_call_index = index;
        const { tool_call, tool, args } = validated[index];
        const output = await tool.execute({
          world,
          args,
          undo_journal: this.undo_journal,
          request_id: request.request_id,
        });
        executed.push({
          tool_call,
          tool,
          output: {
            data: output?.data ?? null,
            changes: Array.isArray(output?.changes) ? output.changes : [],
          },
        });
      }

      const mutates_world = executed.some(({ tool }) => tool.mutates_world);
      const changes = executed.flatMap(({ output }) => output.changes);

      if (dry_run) {
        world.restoreState(world_state);
        this.undo_journal.restoreState(journal_state);
      } else if (mutates_world) {
        world.revision = before_revision + 1;
        world.history.push({
          request_id: request.request_id,
          before_revision,
          after_revision: world.revision,
          changes: clone(changes),
          timestamp_ms: Date.now(),
        });
      }

      const after_revision = world.revision;
      const creates_undo =
        !dry_run &&
        mutates_world &&
        executed.some(({ tool }) => tool.undoable) &&
        !executed.some(({ tool }) => tool.name === "history.undo");
      const undo_token = creates_undo
        ? this.undo_journal.createRecord({
            world_id: world.world_id,
            request_id: request.request_id,
            before_state: world_state,
            changes,
            before_revision,
            after_revision,
          })
        : null;

      const tool_results = executed.map(({ tool_call, tool, output }) => ({
        ok: true,
        request_id: request.request_id,
        tool_call_id: tool_call.tool_call_id,
        before_revision,
        after_revision,
        changes: clone(output.changes),
        undo_token:
          undo_token && tool.mutates_world && tool.undoable ? undo_token : null,
        error: null,
        data: clone(output.data),
        dry_run,
      }));

      return {
        ok: true,
        request_id: request.request_id,
        tool_results,
        world_revision: after_revision,
        undo_token,
        error: null,
        replayed: false,
      };
    } catch (error) {
      const agent_error = asAgentError(error, "EXECUTION_FAILED");
      if (world_state) {
        world.restoreState(world_state);
      }
      if (journal_state) {
        this.undo_journal.restoreState(journal_state);
      }

      const tool_call =
        failed_tool_call_index === null
          ? null
          : request.tool_calls?.[failed_tool_call_index];
      const rolled_back_results = executed.map(({ tool_call: executed_call }) => ({
        ...failureResult({
          request_id: request.request_id,
          tool_call_id: executed_call.tool_call_id,
          revision: before_revision,
          error: new AgentError(
            "EXECUTION_FAILED",
            "Transaction rolled back because a later tool call failed",
            { failed_tool_call_index }
          ),
        }),
        rolled_back: true,
      }));
      rolled_back_results.push(
        failureResult({
          request_id: request.request_id,
          tool_call_id: tool_call?.tool_call_id ?? null,
          revision: before_revision,
          error: agent_error,
        })
      );

      return {
        ok: false,
        request_id: request.request_id ?? null,
        tool_results: rolled_back_results,
        world_revision: before_revision,
        undo_token: null,
        error: agent_error.toJSON(),
        failed_tool_call_index,
        replayed: false,
      };
    }
  }

  #failureResponse(request, revision, error) {
    return {
      ok: false,
      request_id: request?.request_id ?? null,
      tool_results: [
        failureResult({
          request_id: request?.request_id,
          tool_call_id: request?.tool_calls?.[0]?.tool_call_id ?? null,
          revision,
          error,
        }),
      ],
      world_revision: revision,
      undo_token: null,
      error: error.toJSON(),
      failed_tool_call_index: null,
      replayed: false,
    };
  }

  async #writeAudit(request, result, started_at) {
    try {
      await this.audit_log.write({
        request_id: request?.request_id,
        session_id: request?.session_id,
        provider: request?.provider || "direct",
        user_message: request?.user_message ?? null,
        before_revision:
          result.tool_results?.[0]?.before_revision ?? result.world_revision,
        after_revision: result.world_revision,
        tool_calls: request?.tool_calls || [],
        tool_results: result.tool_results,
        error: result.error,
        duration_ms: Date.now() - started_at,
        failed_tool_call_index: result.failed_tool_call_index ?? null,
      });
    } catch (error) {
      console.error(
        "[CattyAgentBridge] audit log write failed:",
        error?.message || error
      );
    }
  }
}
