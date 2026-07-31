import { randomUUID } from "node:crypto";
import {
  WorldAdapterError,
  worldAdapterErrorReasons,
} from "./world-adapter-errors.mjs";
import { MockWorldAdapter } from "./mock-world-adapter.mjs";
import { RemoteWorldAdapter } from "./remote-world-adapter.mjs";
import { normalizeRemoteWorldBaseUrl } from "./remote-world-client.mjs";

export const WORLD_ADAPTER_IDS = Object.freeze(["mock", "remote"]);

export const worldAdapterConfigDefaults = Object.freeze({
  adapter_id: "mock",
  base_url: "http://127.0.0.1:8770",
  timeout_ms: 5_000,
  auth_token: "",
  allow_non_loopback: false,
});

function trimmed(value) {
  return String(value ?? "").trim();
}

function configError(message, field, details = {}) {
  return new WorldAdapterError(
    worldAdapterErrorReasons.CONFIGURATION_ERROR,
    message,
    {
      adapter: "remote",
      details: { field, ...details },
    }
  );
}

function parseTimeout(env) {
  const raw = trimmed(env.MAHO_WORLD_TIMEOUT_MS);
  if (!raw) {
    return worldAdapterConfigDefaults.timeout_ms;
  }
  const value = Number(raw);
  if (
    !Number.isSafeInteger(value) ||
    value < 1 ||
    value > 300_000
  ) {
    throw configError(
      "MAHO_WORLD_TIMEOUT_MS must be an integer from 1 through 300000",
      "MAHO_WORLD_TIMEOUT_MS"
    );
  }
  return value;
}

function parseNonLoopback(env) {
  const raw = trimmed(env.MAHO_WORLD_ALLOW_NON_LOOPBACK);
  if (!raw || raw === "0") {
    return false;
  }
  if (raw === "1") {
    return true;
  }
  throw configError(
    "MAHO_WORLD_ALLOW_NON_LOOPBACK must be 0 or 1",
    "MAHO_WORLD_ALLOW_NON_LOOPBACK"
  );
}

export function resolveWorldAdapterConfig({
  env = process.env,
} = {}) {
  const adapter_id =
    trimmed(env.MAHO_WORLD_ADAPTER).toLowerCase() ||
    worldAdapterConfigDefaults.adapter_id;
  if (!WORLD_ADAPTER_IDS.includes(adapter_id)) {
    throw configError(
      `Unknown WorldAdapter: ${adapter_id}`,
      "MAHO_WORLD_ADAPTER"
    );
  }

  const config = {
    adapter_id,
    base_url:
      trimmed(env.MAHO_WORLD_BASE_URL) ||
      worldAdapterConfigDefaults.base_url,
    timeout_ms: parseTimeout(env),
    auth_token: trimmed(env.MAHO_WORLD_AUTH_TOKEN),
    allow_non_loopback: parseNonLoopback(env),
  };
  if (adapter_id === "remote") {
    const normalized = normalizeRemoteWorldBaseUrl(config.base_url, {
      allow_non_loopback: config.allow_non_loopback,
      auth_token: config.auth_token,
    });
    config.base_url = normalized.base_url;
    config.endpoint_origin = normalized.endpoint_origin;
    config.loopback = normalized.loopback;
  } else {
    config.endpoint_origin = null;
    config.loopback = true;
  }
  return config;
}

export class WorldAdapterFactory {
  constructor({
    config = resolveWorldAdapterConfig(),
    tool_registry,
    fetch_impl = globalThis.fetch,
    mock_adapter_factory,
    remote_adapter_factory,
  } = {}) {
    if (!tool_registry) {
      throw new TypeError("WorldAdapterFactory requires ToolRegistry");
    }
    this.config = config;
    this.tool_registry = tool_registry;
    this.fetch_impl = fetch_impl;
    this.mock_adapter_factory =
      mock_adapter_factory ||
      ((options) => new MockWorldAdapter(options));
    this.remote_adapter_factory =
      remote_adapter_factory ||
      ((options) => new RemoteWorldAdapter(options));
    this.adapters = new Set();
    this.closed = false;
  }

  createForSession({
    session_id = randomUUID(),
    world_id = randomUUID(),
  } = {}) {
    if (this.closed) {
      throw new WorldAdapterError(
        worldAdapterErrorReasons.ADAPTER_UNAVAILABLE,
        "WorldAdapterFactory is closed",
        { adapter: this.config.adapter_id }
      );
    }
    let adapter;
    if (this.config.adapter_id === "mock") {
      adapter = this.mock_adapter_factory({
        session_id,
        world_id,
        tool_registry: this.tool_registry,
      });
    } else if (this.config.adapter_id === "remote") {
      adapter = this.remote_adapter_factory({
        session_id,
        world_id,
        tool_registry: this.tool_registry,
        base_url: this.config.base_url,
        timeout_ms: this.config.timeout_ms,
        auth_token: this.config.auth_token,
        allow_non_loopback: this.config.allow_non_loopback,
        fetch_impl: this.fetch_impl,
      });
    } else {
      throw configError(
        `Unknown WorldAdapter: ${this.config.adapter_id}`,
        "MAHO_WORLD_ADAPTER"
      );
    }
    this.adapters.add(adapter);
    return adapter;
  }

  getMetadata() {
    return {
      adapter: this.config.adapter_id,
      remote: this.config.adapter_id === "remote",
      ready: !this.closed,
      base_url:
        this.config.adapter_id === "remote"
          ? this.config.base_url
          : null,
      endpoint_origin: this.config.endpoint_origin ?? null,
      timeout_ms: this.config.timeout_ms,
      allow_non_loopback: this.config.allow_non_loopback,
    };
  }

  async release(adapter) {
    if (!adapter) {
      return;
    }
    this.adapters.delete(adapter);
    await adapter.close();
  }

  async close() {
    if (this.closed) {
      return;
    }
    this.closed = true;
    const adapters = [...this.adapters];
    this.adapters.clear();
    await Promise.allSettled(
      adapters.map((adapter) => adapter.close())
    );
  }
}
