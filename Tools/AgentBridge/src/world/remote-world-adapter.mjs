import { AgentError } from "../protocol/errors.mjs";
import {
  ADAPTER_PROTOCOL_VERSION,
  assertJsonDto,
  assertWorldAdapterContract,
  validateAdapterCapabilities,
  validateAdapterCapabilityRequest,
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
} from "./world-adapter-errors.mjs";
import { RemoteWorldClient } from "./remote-world-client.mjs";

const HEALTH_CACHE_TTL_MS = 1_000;

function clone(value) {
  return structuredClone(value);
}

function requireObject(value, label, phase) {
  assertJsonDto(value, label);
  if (!value || typeof value !== "object" || Array.isArray(value)) {
    throw new WorldAdapterError(
      worldAdapterErrorReasons.RESPONSE_INVALID,
      `${label} must be an object`,
      {
        adapter: "remote",
        protocol_version: ADAPTER_PROTOCOL_VERSION,
        phase,
        details: { field: label },
      }
    );
  }
  return value;
}

function requireSafeRevision(value, field, phase) {
  if (!Number.isSafeInteger(value) || value < 0) {
    throw new WorldAdapterError(
      worldAdapterErrorReasons.RESPONSE_INVALID,
      `Remote world response ${field} is invalid`,
      {
        adapter: "remote",
        protocol_version: ADAPTER_PROTOCOL_VERSION,
        phase,
        details: { field },
      }
    );
  }
}

function responseReason(error_code) {
  switch (error_code) {
    case "REVISION_CONFLICT":
      return worldAdapterErrorReasons.REVISION_CONFLICT;
    case "ENTITY_NOT_FOUND":
      return worldAdapterErrorReasons.ENTITY_NOT_FOUND;
    case "UNKNOWN_TOOL":
      return worldAdapterErrorReasons.UNKNOWN_TOOL;
    case "INVALID_ARGUMENT":
    case "INVALID_REQUEST":
      return worldAdapterErrorReasons.INVALID_ARGUMENT;
    case "UNDO_NOT_AVAILABLE":
      return worldAdapterErrorReasons.UNDO_NOT_AVAILABLE;
    case "TIMEOUT":
      return worldAdapterErrorReasons.REQUEST_TIMEOUT;
    default:
      return worldAdapterErrorReasons.TRANSACTION_FAILED;
  }
}

export class RemoteWorldAdapter {
  constructor({
    session_id,
    world_id,
    tool_registry,
    base_url,
    timeout_ms = 5_000,
    auth_token = "",
    allow_non_loopback = false,
    fetch_impl = globalThis.fetch,
    client,
    health_cache_ttl_ms = HEALTH_CACHE_TTL_MS,
  }) {
    if (!session_id || !world_id) {
      throw new TypeError(
        "RemoteWorldAdapter requires session_id and world_id"
      );
    }
    if (!tool_registry) {
      throw new TypeError("RemoteWorldAdapter requires ToolRegistry");
    }
    this.name = "remote";
    this.protocolVersion = ADAPTER_PROTOCOL_VERSION;
    this.session_id = session_id;
    this.world_id = world_id;
    this.tool_registry = tool_registry;
    this.known_tools = tool_registry
      .listDefinitions()
      .map((definition) => definition.name);
    this.client = client || new RemoteWorldClient({
      base_url,
      timeout_ms,
      auth_token,
      allow_non_loopback,
      fetch_impl,
    });
    this.health_cache_ttl_ms = health_cache_ttl_ms;
    this.health_cache = null;
    this.health_cache_expires_at = 0;
    this.latest_undo_token = null;
    this.closed = false;
    this.ready = false;
    this.capabilities_negotiated = false;
    this.capabilities = Object.freeze({
      supports_atomic_transactions: true,
      supports_dry_run: true,
      supports_undo: true,
      supports_idempotency: true,
      max_tool_calls: 16,
      supported_tools: Object.freeze([...this.known_tools]),
    });
    assertWorldAdapterContract(this);
  }

  getMetadata() {
    const client_metadata = this.client.getSafeMetadata();
    return {
      adapter: this.name,
      adapter_protocol_version: this.protocolVersion,
      ready: this.ready && !this.closed,
      remote: true,
      session_id: this.session_id,
      world_id: this.world_id,
      base_url: client_metadata.base_url,
      endpoint_origin: client_metadata.endpoint_origin,
      loopback: client_metadata.loopback,
      timeout_ms: client_metadata.timeout_ms,
      capabilities: this.capabilities_negotiated
        ? clone(this.capabilities)
        : null,
      capabilities_negotiated: this.capabilities_negotiated,
    };
  }

  async health({ signal, force = false } = {}) {
    this.#assertOpen("health");
    if (
      !force &&
      this.health_cache &&
      Date.now() < this.health_cache_expires_at
    ) {
      return clone(this.health_cache);
    }
    const response = await this.client.request({
      method: "GET",
      pathname: "/world-adapter/v1/health",
      signal,
      phase: "health",
    });
    if (!response.ok) {
      throw this.#httpError(response.status, response.payload, "health");
    }
    const result = validateHealthResult(response.payload);
    this.#assertProtocolVersion(result, "health");
    this.#acceptCapabilities(result.capabilities);
    this.ready = result.ok;
    if (!result.ok) {
      throw this.#responseError(result, response.status, "health");
    }
    this.health_cache = clone(result);
    this.health_cache_expires_at =
      Date.now() + this.health_cache_ttl_ms;
    return clone(result);
  }

  async getSnapshot(input) {
    validateGetSnapshotInput(input);
    this.#assertOpen("snapshot");
    this.#assertIdentity(input);
    await this.health({ signal: input.signal });
    const response = await this.client.request({
      method: "POST",
      pathname: "/world-adapter/v1/snapshot",
      body: {
        adapter_protocol_version: this.protocolVersion,
        request_id: input.request_id,
        session_id: input.session_id,
        world_id: input.world_id,
      },
      signal: input.signal,
      phase: "snapshot",
    });
    if (!response.ok) {
      throw this.#httpError(
        response.status,
        response.payload,
        "snapshot"
      );
    }
    const payload = requireObject(
      response.payload,
      "snapshot response",
      "snapshot"
    );
    this.#assertProtocolVersion(payload, "snapshot");
    this.#assertCorrelation(payload, input, "snapshot");
    if (payload.ok !== true) {
      throw this.#responseError(payload, response.status, "snapshot");
    }
    requireSafeRevision(
      payload.world_revision,
      "world_revision",
      "snapshot"
    );
    if (
      !Number.isSafeInteger(payload.timestamp_ms) ||
      payload.timestamp_ms < 0 ||
      !Array.isArray(payload.entities)
    ) {
      throw new WorldAdapterError(
        worldAdapterErrorReasons.RESPONSE_INVALID,
        "Remote snapshot response is missing required fields",
        {
          adapter: this.name,
          protocol_version: this.protocolVersion,
          phase: "snapshot",
        }
      );
    }
    if (payload.capabilities !== undefined) {
      this.#acceptCapabilities(payload.capabilities);
    }
    return validateWorldSnapshot(
      {
        world_id: payload.world_id,
        revision: payload.world_revision,
        entities: clone(payload.entities),
        history: Array.isArray(payload.history)
          ? clone(payload.history)
          : [],
        ...(payload.adapter_metadata &&
        typeof payload.adapter_metadata === "object" &&
        !Array.isArray(payload.adapter_metadata)
          ? { adapter_metadata: clone(payload.adapter_metadata) }
          : {}),
      },
      {
        expected_world_id: input.world_id,
        phase: "snapshot",
      }
    );
  }

  async executeTransaction(input) {
    validateExecuteTransactionInput(input);
    this.#assertOpen("execute");
    this.#assertIdentity(input);
    await this.health({ signal: input.signal });
    validateAdapterCapabilityRequest(this.capabilities, {
      tool_calls: input.tool_calls,
      dry_run: input.dry_run,
      atomic: input.atomic,
      phase: "execute",
      known_tools: this.known_tools,
    });

    const started_at = performance.now();
    const response = await this.client.request({
      method: "POST",
      pathname: "/world-adapter/v1/execute",
      body: {
        adapter_protocol_version: this.protocolVersion,
        request_id: input.request_id,
        session_id: input.session_id,
        world_id: input.world_id,
        expected_revision: input.expected_revision,
        dry_run: input.dry_run,
        atomic: input.atomic,
        tool_calls: clone(input.tool_calls),
      },
      signal: input.signal,
      phase: "execute",
    });
    if (
      !response.ok &&
      !this.#looksLikeTransactionResponse(response.payload)
    ) {
      throw this.#httpError(
        response.status,
        response.payload,
        "execute"
      );
    }
    const payload = requireObject(
      response.payload,
      "execute response",
      "execute"
    );
    this.#assertProtocolVersion(payload, "execute");
    const normalized = {
      ok: payload.ok,
      request_id: payload.request_id,
      session_id: payload.session_id,
      world_id: payload.world_id,
      before_revision: payload.before_revision,
      after_revision: payload.after_revision,
      replayed: payload.replayed,
      tool_results: payload.tool_results,
      changes: payload.changes,
      undo_token: payload.undo_token,
      error: payload.error,
      failed_tool_call_index:
        payload.failed_tool_call_index ?? null,
      adapter_metadata: this.#operationMetadata(
        "execute",
        started_at,
        response.status,
        payload.replayed
      ),
    };
    const result = validateExecuteTransactionResult(normalized, input);
    if (result.ok && !input.dry_run && result.undo_token) {
      this.latest_undo_token = result.undo_token;
    }
    return result;
  }

  async undo(input) {
    validateUndoInput(input);
    this.#assertOpen("undo");
    this.#assertIdentity(input);
    await this.health({ signal: input.signal });
    validateAdapterCapabilityRequest(this.capabilities, {
      tool_calls: [{ tool_name: "history.undo" }],
      dry_run: false,
      atomic: false,
      phase: "undo",
      known_tools: this.known_tools,
    });
    const undo_token = input.undo_token || this.latest_undo_token;
    if (!undo_token) {
      return validateUndoResult(
        {
          ok: false,
          request_id: input.request_id,
          session_id: input.session_id,
          world_id: input.world_id,
          before_revision: input.expected_revision,
          after_revision: input.expected_revision,
          replayed: false,
          changes: [],
          undo_token: null,
          error: new AgentError(
            "UNDO_NOT_AVAILABLE",
            "No remote undo token is available"
          ).toJSON(),
          data: null,
          adapter_metadata: this.#operationMetadata(
            "undo",
            performance.now(),
            null,
            false
          ),
        },
        input
      );
    }

    const started_at = performance.now();
    const response = await this.client.request({
      method: "POST",
      pathname: "/world-adapter/v1/undo",
      body: {
        adapter_protocol_version: this.protocolVersion,
        request_id: input.request_id,
        session_id: input.session_id,
        world_id: input.world_id,
        expected_revision: input.expected_revision,
        undo_token,
      },
      signal: input.signal,
      phase: "undo",
    });
    if (
      !response.ok &&
      !this.#looksLikeUndoResponse(response.payload)
    ) {
      throw this.#httpError(response.status, response.payload, "undo");
    }
    const payload = requireObject(
      response.payload,
      "undo response",
      "undo"
    );
    this.#assertProtocolVersion(payload, "undo");
    const normalized = {
      ok: payload.ok,
      request_id: payload.request_id,
      session_id: payload.session_id,
      world_id: payload.world_id,
      before_revision: payload.before_revision,
      after_revision: payload.after_revision,
      replayed: payload.replayed,
      changes: payload.changes,
      undo_token: payload.undo_token,
      error: payload.error,
      data: payload.data ?? {
        undone_token: undo_token,
      },
      adapter_metadata: this.#operationMetadata(
        "undo",
        started_at,
        response.status,
        payload.replayed
      ),
    };
    const result = validateUndoResult(normalized, input);
    if (result.ok) {
      this.latest_undo_token = null;
    }
    return result;
  }

  async close() {
    if (this.closed) {
      return;
    }
    this.closed = true;
    this.ready = false;
    this.health_cache = null;
    this.health_cache_expires_at = 0;
    await this.client.close();
  }

  #acceptCapabilities(capabilities) {
    const validated = validateAdapterCapabilities(capabilities, {
      phase: "health",
      known_tools: this.known_tools,
    });
    if (!validated.supports_idempotency) {
      throw new WorldAdapterError(
        worldAdapterErrorReasons.CAPABILITY_INSUFFICIENT,
        "Remote world request replay requires idempotency support",
        {
          adapter: this.name,
          protocol_version: this.protocolVersion,
          phase: "health",
          details: {
            field: "capabilities.supports_idempotency",
          },
        }
      );
    }
    this.capabilities = Object.freeze({
      ...validated,
      supported_tools: Object.freeze([...validated.supported_tools]),
    });
    this.capabilities_negotiated = true;
  }

  #assertProtocolVersion(payload, phase) {
    if (payload.adapter_protocol_version !== this.protocolVersion) {
      throw new WorldAdapterError(
        worldAdapterErrorReasons.PROTOCOL_VERSION_INCOMPATIBLE,
        "Remote world adapter protocol version is incompatible",
        {
          adapter: this.name,
          protocol_version: this.protocolVersion,
          phase,
          details: {
            expected: this.protocolVersion,
            received: payload.adapter_protocol_version ?? null,
          },
        }
      );
    }
  }

  #assertCorrelation(payload, input, phase) {
    for (const field of ["request_id", "session_id", "world_id"]) {
      if (payload[field] !== input[field]) {
        throw new WorldAdapterError(
          worldAdapterErrorReasons.CORRELATION_MISMATCH,
          `Remote world response ${field} does not match the request`,
          {
            adapter: this.name,
            protocol_version: this.protocolVersion,
            phase,
            details: {
              field,
              expected: input[field],
              received: payload[field],
            },
          }
        );
      }
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
        "RemoteWorldAdapter is closed",
        {
          adapter: this.name,
          protocol_version: this.protocolVersion,
          phase,
        }
      );
    }
  }

  #looksLikeTransactionResponse(payload) {
    return Boolean(
      payload &&
      typeof payload === "object" &&
      !Array.isArray(payload) &&
      "before_revision" in payload &&
      "after_revision" in payload &&
      Array.isArray(payload.tool_results)
    );
  }

  #looksLikeUndoResponse(payload) {
    return Boolean(
      payload &&
      typeof payload === "object" &&
      !Array.isArray(payload) &&
      "before_revision" in payload &&
      "after_revision" in payload &&
      Array.isArray(payload.changes)
    );
  }

  #responseError(payload, status, phase) {
    const error = payload?.error;
    const code =
      error && typeof error === "object" ? error.code : null;
    const message =
      error && typeof error.message === "string"
        ? error.message
        : "Remote world operation failed";
    return new WorldAdapterError(responseReason(code), message, {
      adapter: this.name,
      protocol_version: this.protocolVersion,
      phase,
      http_status: status,
      retryable: Boolean(error?.retryable),
      timeout: code === "TIMEOUT",
      details: { http_status: status },
    });
  }

  #httpError(status, payload, phase) {
    if (
      payload &&
      typeof payload === "object" &&
      !Array.isArray(payload) &&
      payload.error
    ) {
      return this.#responseError(payload, status, phase);
    }
    let reason = worldAdapterErrorReasons.HTTP_ERROR;
    let retryable = status === 408 || status === 429 || status >= 500;
    let timeout = false;
    if (status === 408) {
      reason = worldAdapterErrorReasons.REQUEST_TIMEOUT;
      timeout = true;
    } else if (status === 409) {
      reason = worldAdapterErrorReasons.REVISION_CONFLICT;
      retryable = true;
    }
    return new WorldAdapterError(
      reason,
      `Remote world request failed with HTTP ${status}`,
      {
        adapter: this.name,
        protocol_version: this.protocolVersion,
        phase,
        http_status: status,
        retryable,
        timeout,
        details: { http_status: status },
      }
    );
  }

  #operationMetadata(
    phase,
    started_at,
    http_status,
    replayed
  ) {
    return {
      adapter: this.name,
      adapter_protocol_version: this.protocolVersion,
      adapter_request_phase: phase,
      adapter_duration_ms: Math.max(
        0,
        Math.round(performance.now() - started_at)
      ),
      adapter_http_status: http_status,
      adapter_replayed: Boolean(replayed),
      adapter_timeout: false,
      adapter_cancelled: false,
      remote_error_class: null,
    };
  }
}
