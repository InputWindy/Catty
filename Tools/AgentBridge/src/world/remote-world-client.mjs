import {
  WorldAdapterError,
  worldAdapterErrorReasons,
} from "./world-adapter-errors.mjs";

const DEFAULT_RESPONSE_LIMIT_BYTES = 4 * 1024 * 1024;

export function isLoopbackHostname(hostname) {
  const normalized = String(hostname || "").toLowerCase();
  return normalized === "127.0.0.1" ||
    normalized === "localhost" ||
    normalized === "[::1]" ||
    normalized === "::1";
}

export function normalizeRemoteWorldBaseUrl(
  base_url,
  {
    allow_non_loopback = false,
    auth_token = "",
  } = {}
) {
  let parsed;
  try {
    parsed = new URL(base_url);
  } catch {
    throw new WorldAdapterError(
      worldAdapterErrorReasons.INVALID_BASE_URL,
      "MAHO_WORLD_BASE_URL must be a valid HTTP or HTTPS URL",
      { details: { field: "MAHO_WORLD_BASE_URL" } }
    );
  }
  if (
    !["http:", "https:"].includes(parsed.protocol) ||
    parsed.username ||
    parsed.password ||
    parsed.search ||
    parsed.hash
  ) {
    throw new WorldAdapterError(
      worldAdapterErrorReasons.INVALID_BASE_URL,
      "MAHO_WORLD_BASE_URL must be a credential-free HTTP or HTTPS URL without query or fragment",
      { details: { field: "MAHO_WORLD_BASE_URL" } }
    );
  }

  const loopback = isLoopbackHostname(parsed.hostname);
  if (!loopback && !allow_non_loopback) {
    throw new WorldAdapterError(
      worldAdapterErrorReasons.NON_LOOPBACK_REJECTED,
      "Non-loopback world URLs require MAHO_WORLD_ALLOW_NON_LOOPBACK=1",
      {
        details: {
          field: "MAHO_WORLD_ALLOW_NON_LOOPBACK",
          endpoint_origin: parsed.origin,
        },
      }
    );
  }
  if (!loopback && !String(auth_token || "").trim()) {
    throw new WorldAdapterError(
      worldAdapterErrorReasons.AUTH_REQUIRED,
      "Non-loopback world URLs require MAHO_WORLD_AUTH_TOKEN",
      {
        details: {
          field: "MAHO_WORLD_AUTH_TOKEN",
          endpoint_origin: parsed.origin,
        },
      }
    );
  }

  parsed.pathname = parsed.pathname.replace(/\/+$/, "");
  return {
    base_url: parsed.toString().replace(/\/$/, ""),
    endpoint_origin: parsed.origin,
    loopback,
  };
}

export class RemoteWorldClient {
  constructor({
    base_url,
    timeout_ms = 5_000,
    auth_token = "",
    allow_non_loopback = false,
    fetch_impl = globalThis.fetch,
    response_limit_bytes = DEFAULT_RESPONSE_LIMIT_BYTES,
  }) {
    if (typeof fetch_impl !== "function") {
      throw new TypeError("RemoteWorldClient requires fetch");
    }
    const normalized = normalizeRemoteWorldBaseUrl(base_url, {
      allow_non_loopback,
      auth_token,
    });
    this.base_url = normalized.base_url;
    this.endpoint_origin = normalized.endpoint_origin;
    this.loopback = normalized.loopback;
    this.timeout_ms = timeout_ms;
    this.auth_token = String(auth_token || "");
    this.fetch_impl = fetch_impl;
    this.response_limit_bytes = response_limit_bytes;
    this.active_requests = new Map();
    this.closed = false;
  }

  getSafeMetadata() {
    return {
      base_url: this.base_url,
      endpoint_origin: this.endpoint_origin,
      loopback: this.loopback,
      timeout_ms: this.timeout_ms,
      ready: !this.closed,
    };
  }

  async request({
    method,
    pathname,
    body,
    signal,
    phase,
  }) {
    if (this.closed) {
      throw new WorldAdapterError(
        worldAdapterErrorReasons.ADAPTER_UNAVAILABLE,
        "Remote world client is closed",
        {
          adapter: "remote",
          phase,
          details: { endpoint_origin: this.endpoint_origin },
        }
      );
    }
    if (signal?.aborted) {
      throw new WorldAdapterError(
        worldAdapterErrorReasons.REQUEST_CANCELLED,
        "Remote world request was cancelled",
        {
          adapter: "remote",
          phase,
          cancelled: true,
          details: { endpoint_origin: this.endpoint_origin },
        }
      );
    }

    const controller = new AbortController();
    const state = { timeout: false, shutdown: false };
    this.active_requests.set(controller, state);
    const onExternalAbort = () => controller.abort(signal.reason);
    signal?.addEventListener("abort", onExternalAbort, { once: true });
    const timer = setTimeout(() => {
      state.timeout = true;
      controller.abort(new Error("Remote world request timed out"));
    }, this.timeout_ms);

    try {
      const headers = {
        Accept: "application/json",
      };
      if (body !== undefined) {
        headers["Content-Type"] = "application/json";
      }
      if (this.auth_token) {
        headers.Authorization = `Bearer ${this.auth_token}`;
      }
      const response = await this.fetch_impl(
        `${this.base_url}${pathname}`,
        {
          method,
          headers,
          ...(body === undefined
            ? {}
            : { body: JSON.stringify(body) }),
          signal: controller.signal,
        }
      );

      const content_length = Number(
        response.headers.get("content-length") || "0"
      );
      if (
        Number.isFinite(content_length) &&
        content_length > this.response_limit_bytes
      ) {
        throw new WorldAdapterError(
          worldAdapterErrorReasons.RESPONSE_INVALID,
          "Remote world response exceeds the configured size limit",
          {
            adapter: "remote",
            phase,
            http_status: response.status,
            details: {
              http_status: response.status,
              endpoint_origin: this.endpoint_origin,
            },
          }
        );
      }

      let text;
      try {
        text = await response.text();
      } catch (error) {
        throw new WorldAdapterError(
          worldAdapterErrorReasons.CONNECTION_FAILED,
          "Remote world connection ended before the response completed",
          {
            adapter: "remote",
            phase,
            http_status: response.status,
            retryable: true,
            details: {
              http_status: response.status,
              endpoint_origin: this.endpoint_origin,
            },
            cause: error,
          }
        );
      }
      if (Buffer.byteLength(text, "utf8") > this.response_limit_bytes) {
        throw new WorldAdapterError(
          worldAdapterErrorReasons.RESPONSE_INVALID,
          "Remote world response exceeds the configured size limit",
          {
            adapter: "remote",
            phase,
            http_status: response.status,
            details: {
              http_status: response.status,
              endpoint_origin: this.endpoint_origin,
            },
          }
        );
      }

      let payload;
      try {
        payload = text ? JSON.parse(text) : {};
      } catch (error) {
        if (!response.ok) {
          throw this.#httpError(response.status, phase, error);
        }
        throw new WorldAdapterError(
          worldAdapterErrorReasons.RESPONSE_INVALID_JSON,
          "Remote world response was not valid JSON",
          {
            adapter: "remote",
            phase,
            http_status: response.status,
            details: {
              http_status: response.status,
              endpoint_origin: this.endpoint_origin,
            },
            cause: error,
          }
        );
      }
      return {
        payload,
        status: response.status,
        ok: response.ok,
      };
    } catch (error) {
      if (error instanceof WorldAdapterError) {
        throw error;
      }
      if (state.timeout) {
        throw new WorldAdapterError(
          worldAdapterErrorReasons.REQUEST_TIMEOUT,
          `Remote world request timed out after ${this.timeout_ms} ms`,
          {
            adapter: "remote",
            phase,
            timeout: true,
            details: { endpoint_origin: this.endpoint_origin },
            cause: error,
          }
        );
      }
      if (state.shutdown || signal?.aborted || controller.signal.aborted) {
        throw new WorldAdapterError(
          worldAdapterErrorReasons.REQUEST_CANCELLED,
          "Remote world request was cancelled",
          {
            adapter: "remote",
            phase,
            cancelled: true,
            details: { endpoint_origin: this.endpoint_origin },
            cause: error,
          }
        );
      }
      throw new WorldAdapterError(
        worldAdapterErrorReasons.CONNECTION_FAILED,
        "Remote world connection failed",
        {
          adapter: "remote",
          phase,
          retryable: true,
          details: { endpoint_origin: this.endpoint_origin },
          cause: error,
        }
      );
    } finally {
      clearTimeout(timer);
      signal?.removeEventListener("abort", onExternalAbort);
      this.active_requests.delete(controller);
    }
  }

  async close() {
    this.closed = true;
    for (const [controller, state] of this.active_requests) {
      state.shutdown = true;
      controller.abort(new Error("Remote world client shutdown"));
    }
  }

  #httpError(status, phase, cause) {
    let reason = worldAdapterErrorReasons.HTTP_ERROR;
    let message = `Remote world request failed with HTTP ${status}`;
    let retryable = status === 408 || status === 429 || status >= 500;
    let timeout = false;
    if (status === 408) {
      reason = worldAdapterErrorReasons.REQUEST_TIMEOUT;
      timeout = true;
      message = "Remote world service returned HTTP 408";
    } else if (status === 409) {
      reason = worldAdapterErrorReasons.REVISION_CONFLICT;
      retryable = true;
    }
    return new WorldAdapterError(reason, message, {
      adapter: "remote",
      phase,
      http_status: status,
      retryable,
      timeout,
      details: {
        http_status: status,
        endpoint_origin: this.endpoint_origin,
      },
      cause,
    });
  }
}
