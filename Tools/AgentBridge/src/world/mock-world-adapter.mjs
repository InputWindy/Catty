import { AgentError, asAgentError } from "../protocol/errors.mjs";
import { UndoJournal } from "../history/undo-journal.mjs";
import { MockWorld } from "./mock-world.mjs";
import { createMockWorldCommandHandlers } from "./mock-world-commands.mjs";
import {
  ADAPTER_PROTOCOL_VERSION,
  assertWorldAdapterContract,
  validateExecuteTransactionInput,
  validateExecuteTransactionResult,
  validateGetSnapshotInput,
  validateHealthResult,
  validateUndoInput,
  validateUndoResult,
  validateWorldSnapshot,
} from "./world-adapter-contract.mjs";
import {
  WorldAdapterError,
  worldAdapterErrorReasons,
  worldAdapterErrorToAgentError,
} from "./world-adapter-errors.mjs";

function clone(value) {
  return structuredClone(value);
}

function failureToolResult({
  request_id,
  tool_call_id = null,
  revision,
  error,
  dry_run = false,
  rolled_back = false,
}) {
  return {
    ok: false,
    request_id,
    tool_call_id,
    before_revision: revision,
    after_revision: revision,
    changes: [],
    undo_token: null,
    error: error.toJSON(),
    data: null,
    dry_run,
    ...(rolled_back ? { rolled_back: true } : {}),
  };
}

function toAgentError(error, fallback_code = "EXECUTION_FAILED") {
  if (error instanceof WorldAdapterError) {
    return worldAdapterErrorToAgentError(error);
  }
  return asAgentError(error, fallback_code);
}

export class MockWorldAdapter {
  constructor({
    session_id,
    world_id,
    tool_registry,
    max_tool_calls = 100,
    world = new MockWorld({ world_id }),
    undo_journal = new UndoJournal(),
    handlers = createMockWorldCommandHandlers(),
  }) {
    if (!session_id) {
      throw new TypeError("MockWorldAdapter requires session_id");
    }
    if (!tool_registry) {
      throw new TypeError("MockWorldAdapter requires ToolRegistry");
    }
    this.name = "mock";
    this.protocolVersion = ADAPTER_PROTOCOL_VERSION;
    this.session_id = session_id;
    this.world_id = world_id;
    this.tool_registry = tool_registry;
    this.max_tool_calls = max_tool_calls;
    this.mock_world = world;
    this.undo_journal = undo_journal;
    this.handlers = handlers;
    this.request_results = new Map();
    this.request_promises = new Map();
    this.busy = false;
    this.closed = false;
    this.capabilities = Object.freeze({
      supports_atomic_transactions: true,
      supports_dry_run: true,
      supports_undo: true,
      supports_idempotency: true,
      max_tool_calls,
      supported_tools: Object.freeze(
        tool_registry.listDefinitions().map((definition) => definition.name)
      ),
    });
    assertWorldAdapterContract(this);
  }

  getMetadata() {
    return {
      adapter: this.name,
      adapter_protocol_version: this.protocolVersion,
      ready: !this.closed,
      remote: false,
      session_id: this.session_id,
      world_id: this.world_id,
      capabilities: clone(this.capabilities),
    };
  }

  async health() {
    return validateHealthResult({
      ok: !this.closed,
      adapter_protocol_version: this.protocolVersion,
      server_name: "mock-world",
      server_version: "0.4",
      capabilities: clone(this.capabilities),
      error: this.closed
        ? new AgentError(
            "EXECUTION_FAILED",
            "MockWorldAdapter is closed"
          ).toJSON()
        : null,
    });
  }

  async getSnapshot(input) {
    validateGetSnapshotInput(input);
    this.#assertOpen("snapshot");
    this.#assertIdentity(input);
    return validateWorldSnapshot(this.mock_world.snapshot(), {
      expected_world_id: input.world_id,
      phase: "snapshot",
    });
  }

  async executeTransaction(input) {
    validateExecuteTransactionInput(input);
    this.#assertOpen("execute");
    this.#assertIdentity(input);

    const replay = await this.#getReplay(input.request_id, "execute");
    if (replay) {
      return validateExecuteTransactionResult(replay, input);
    }

    if (input.tool_calls.length > this.capabilities.max_tool_calls) {
      const error = new AgentError(
        "INVALID_ARGUMENT",
        "ToolCall count exceeds the WorldAdapter capability",
        {
          tool_call_count: input.tool_calls.length,
          max_tool_calls: this.capabilities.max_tool_calls,
        }
      );
      const result = this.#failedExecuteResult(input, {
        error,
        revision: this.mock_world.revision,
        failed_tool_call_index: 0,
      });
      this.#cache(input.request_id, "execute", result);
      return validateExecuteTransactionResult(result, input);
    }

    if (this.busy) {
      const error = new AgentError(
        "BUSY",
        "Another transaction is already executing for this world",
        { session_id: this.session_id, world_id: this.world_id },
        true
      );
      const result = this.#failedExecuteResult(input, {
        error,
        revision: this.mock_world.revision,
        failed_tool_call_index: 0,
      });
      this.#cache(input.request_id, "execute", result);
      return validateExecuteTransactionResult(result, input);
    }

    this.busy = true;
    const execution = this.#execute(input);
    this.request_promises.set(input.request_id, {
      operation: "execute",
      promise: execution,
    });
    try {
      const result = await execution;
      this.#cache(input.request_id, "execute", result);
      return validateExecuteTransactionResult(result, input);
    } finally {
      this.request_promises.delete(input.request_id);
      this.busy = false;
    }
  }

  async undo(input) {
    validateUndoInput(input);
    this.#assertOpen("undo");
    this.#assertIdentity(input);

    const replay = await this.#getReplay(input.request_id, "undo");
    if (replay) {
      return validateUndoResult(replay, input);
    }

    if (this.busy) {
      const result = this.#failedUndoResult(
        input,
        new AgentError(
          "BUSY",
          "Another transaction is already executing for this world",
          { session_id: this.session_id, world_id: this.world_id },
          true
        ),
        this.mock_world.revision
      );
      this.#cache(input.request_id, "undo", result);
      return validateUndoResult(result, input);
    }

    this.busy = true;
    const execution = this.#undo(input);
    this.request_promises.set(input.request_id, {
      operation: "undo",
      promise: execution,
    });
    try {
      const result = await execution;
      this.#cache(input.request_id, "undo", result);
      return validateUndoResult(result, input);
    } finally {
      this.request_promises.delete(input.request_id);
      this.busy = false;
    }
  }

  async close() {
    this.closed = true;
  }

  async #execute(input) {
    const started_at = performance.now();
    const before_revision = this.mock_world.revision;
    let world_state;
    let journal_state;
    let failed_tool_call_index = 0;
    const executed = [];

    try {
      if (input.expected_revision !== before_revision) {
        throw new AgentError(
          "REVISION_CONFLICT",
          `Expected revision ${input.expected_revision}, current revision is ${before_revision}`,
          {
            expected_revision: input.expected_revision,
            current_revision: before_revision,
          },
          true
        );
      }

      const validated = [];
      for (const [index, tool_call] of input.tool_calls.entries()) {
        failed_tool_call_index = index;
        const tool = this.tool_registry.get(tool_call.tool_name);
        if (tool.name === "history.undo") {
          throw new AgentError(
            "INVALID_ARGUMENT",
            "history.undo must be routed through WorldAdapter.undo()"
          );
        }
        const handler = this.handlers.get(tool.name);
        if (typeof handler !== "function") {
          throw new AgentError(
            "UNKNOWN_TOOL",
            `MockWorldAdapter has no handler for tool: ${tool.name}`,
            { tool_name: tool.name }
          );
        }
        const args = this.tool_registry.validate(tool.name, tool_call.args);
        validated.push({ tool_call, tool, handler, args });
      }

      world_state = this.mock_world.captureState();
      journal_state = this.undo_journal.captureState();

      for (const [index, entry] of validated.entries()) {
        failed_tool_call_index = index;
        const output = await entry.handler({
          world: this.mock_world,
          args: entry.args,
          request_id: input.request_id,
        });
        executed.push({
          ...entry,
          output: {
            data: output?.data ?? null,
            changes: Array.isArray(output?.changes)
              ? clone(output.changes)
              : [],
          },
        });
      }

      const mutates_world = executed.some(({ tool }) => tool.mutates_world);
      const changes = executed.flatMap(({ output }) => output.changes);

      if (input.dry_run) {
        this.mock_world.restoreState(world_state);
        this.undo_journal.restoreState(journal_state);
      } else if (mutates_world) {
        this.mock_world.revision = before_revision + 1;
        this.mock_world.history.push({
          request_id: input.request_id,
          before_revision,
          after_revision: this.mock_world.revision,
          changes: clone(changes),
          timestamp_ms: Date.now(),
        });
      }

      const after_revision = this.mock_world.revision;
      const creates_undo =
        !input.dry_run &&
        mutates_world &&
        executed.some(({ tool }) => tool.undoable);
      const undo_token = creates_undo
        ? this.undo_journal.createRecord({
            world_id: this.world_id,
            request_id: input.request_id,
            before_state: world_state,
            changes,
            before_revision,
            after_revision,
          })
        : null;

      return {
        ok: true,
        request_id: input.request_id,
        session_id: input.session_id,
        world_id: input.world_id,
        before_revision,
        after_revision,
        replayed: false,
        tool_results: executed.map(({ tool_call, tool, output }) => ({
          ok: true,
          request_id: input.request_id,
          tool_call_id: tool_call.tool_call_id,
          before_revision,
          after_revision,
          changes: clone(output.changes),
          undo_token:
            undo_token && tool.mutates_world && tool.undoable
              ? undo_token
              : null,
          error: null,
          data: clone(output.data),
          dry_run: input.dry_run,
        })),
        changes: clone(changes),
        undo_token,
        error: null,
        failed_tool_call_index: null,
        adapter_metadata: this.#operationMetadata(
          "execute",
          started_at
        ),
      };
    } catch (error) {
      if (world_state) {
        this.mock_world.restoreState(world_state);
      }
      if (journal_state) {
        this.undo_journal.restoreState(journal_state);
      }
      const agent_error = toAgentError(error);
      const tool_call = input.tool_calls[failed_tool_call_index];
      const tool_results = executed.map(({ tool_call: executed_call }) =>
        failureToolResult({
          request_id: input.request_id,
          tool_call_id: executed_call.tool_call_id,
          revision: before_revision,
          error: new AgentError(
            "EXECUTION_FAILED",
            "Transaction rolled back because a later ToolCall failed",
            { failed_tool_call_index }
          ),
          dry_run: input.dry_run,
          rolled_back: true,
        })
      );
      tool_results.push(
        failureToolResult({
          request_id: input.request_id,
          tool_call_id: tool_call?.tool_call_id ?? null,
          revision: before_revision,
          error: agent_error,
          dry_run: input.dry_run,
        })
      );
      return {
        ok: false,
        request_id: input.request_id,
        session_id: input.session_id,
        world_id: input.world_id,
        before_revision,
        after_revision: before_revision,
        replayed: false,
        tool_results,
        changes: [],
        undo_token: null,
        error: agent_error.toJSON(),
        failed_tool_call_index,
        adapter_metadata: this.#operationMetadata(
          "execute",
          started_at,
          agent_error
        ),
      };
    }
  }

  async #undo(input) {
    const started_at = performance.now();
    const before_revision = this.mock_world.revision;
    let world_state;
    let journal_state;
    try {
      if (input.expected_revision !== before_revision) {
        throw new AgentError(
          "REVISION_CONFLICT",
          `Expected revision ${input.expected_revision}, current revision is ${before_revision}`,
          {
            expected_revision: input.expected_revision,
            current_revision: before_revision,
          },
          true
        );
      }
      world_state = this.mock_world.captureState();
      journal_state = this.undo_journal.captureState();
      const output = this.undo_journal.undo(
        this.mock_world,
        input.undo_token ?? undefined
      );
      this.mock_world.revision = before_revision + 1;
      this.mock_world.history.push({
        request_id: input.request_id,
        before_revision,
        after_revision: this.mock_world.revision,
        changes: clone(output.changes),
        timestamp_ms: Date.now(),
      });
      return {
        ok: true,
        request_id: input.request_id,
        session_id: input.session_id,
        world_id: input.world_id,
        before_revision,
        after_revision: this.mock_world.revision,
        replayed: false,
        changes: clone(output.changes),
        undo_token: null,
        error: null,
        data: clone(output.data),
        adapter_metadata: this.#operationMetadata("undo", started_at),
      };
    } catch (error) {
      if (world_state) {
        this.mock_world.restoreState(world_state);
      }
      if (journal_state) {
        this.undo_journal.restoreState(journal_state);
      }
      return this.#failedUndoResult(
        input,
        toAgentError(error),
        before_revision,
        started_at
      );
    }
  }

  #failedExecuteResult(
    input,
    { error, revision, failed_tool_call_index }
  ) {
    return {
      ok: false,
      request_id: input.request_id,
      session_id: input.session_id,
      world_id: input.world_id,
      before_revision: revision,
      after_revision: revision,
      replayed: false,
      tool_results: [
        failureToolResult({
          request_id: input.request_id,
          tool_call_id:
            input.tool_calls[failed_tool_call_index]?.tool_call_id ?? null,
          revision,
          error,
          dry_run: input.dry_run,
        }),
      ],
      changes: [],
      undo_token: null,
      error: error.toJSON(),
      failed_tool_call_index,
      adapter_metadata: this.#operationMetadata("execute", performance.now(), error),
    };
  }

  #failedUndoResult(
    input,
    error,
    revision,
    started_at = performance.now()
  ) {
    return {
      ok: false,
      request_id: input.request_id,
      session_id: input.session_id,
      world_id: input.world_id,
      before_revision: revision,
      after_revision: revision,
      replayed: false,
      changes: [],
      undo_token: null,
      error: error.toJSON(),
      data: null,
      adapter_metadata: this.#operationMetadata(
        "undo",
        started_at,
        error
      ),
    };
  }

  async #getReplay(request_id, operation) {
    const cached = this.request_results.get(request_id);
    if (cached) {
      this.#assertOperation(cached.operation, operation, request_id);
      return { ...clone(cached.result), replayed: true };
    }
    const pending = this.request_promises.get(request_id);
    if (pending) {
      this.#assertOperation(pending.operation, operation, request_id);
      const result = await pending.promise;
      return { ...clone(result), replayed: true };
    }
    return null;
  }

  #cache(request_id, operation, result) {
    this.request_results.set(request_id, {
      operation,
      result: clone(result),
    });
  }

  #assertOperation(actual, expected, request_id) {
    if (actual !== expected) {
      throw new WorldAdapterError(
        worldAdapterErrorReasons.INVALID_ARGUMENT,
        "request_id was already used for a different world operation",
        {
          adapter: this.name,
          protocol_version: this.protocolVersion,
          details: { request_id },
        }
      );
    }
  }

  #assertIdentity(input) {
    if (input.session_id !== this.session_id) {
      throw new AgentError(
        "UNKNOWN_SESSION",
        `Unknown session: ${input.session_id}`,
        { session_id: input.session_id }
      );
    }
    if (input.world_id !== this.world_id) {
      throw new AgentError(
        "UNKNOWN_WORLD",
        `Unknown world: ${input.world_id}`,
        {
          session_id: input.session_id,
          world_id: input.world_id,
        }
      );
    }
  }

  #assertOpen(phase) {
    if (this.closed) {
      throw new WorldAdapterError(
        worldAdapterErrorReasons.ADAPTER_UNAVAILABLE,
        "MockWorldAdapter is closed",
        {
          adapter: this.name,
          protocol_version: this.protocolVersion,
          phase,
        }
      );
    }
  }

  #operationMetadata(phase, started_at, error = null) {
    return {
      adapter: this.name,
      adapter_protocol_version: this.protocolVersion,
      adapter_request_phase: phase,
      adapter_duration_ms: Math.max(
        0,
        Math.round(performance.now() - started_at)
      ),
      adapter_http_status: null,
      adapter_replayed: false,
      adapter_timeout: error?.code === "TIMEOUT",
      adapter_cancelled: false,
      remote_error_class: null,
    };
  }
}
