import http from "node:http";
import { AgentError, errorHttpStatus } from "../../src/protocol/errors.mjs";
import { createDefaultToolRegistry } from "../../src/tools/definitions.mjs";
import {
  ADAPTER_PROTOCOL_VERSION,
  validateExecuteTransactionInput,
  validateGetSnapshotInput,
  validateUndoInput,
} from "../../src/world/world-adapter-contract.mjs";
import {
  MockWorldAdapter,
} from "../../src/world/mock-world-adapter.mjs";
import {
  WorldAdapterError,
  worldAdapterErrorToAgentError,
} from "../../src/world/world-adapter-errors.mjs";

const REQUEST_LIMIT_BYTES = 1024 * 1024;

function clone(value) {
  return structuredClone(value);
}

async function readJson(req) {
  const chunks = [];
  let received = 0;
  for await (const chunk of req) {
    received += chunk.length;
    if (received > REQUEST_LIMIT_BYTES) {
      throw new AgentError(
        "REQUEST_TOO_LARGE",
        "Fake world request body is too large"
      );
    }
    chunks.push(chunk);
  }
  const raw = Buffer.concat(chunks).toString("utf8");
  return raw ? JSON.parse(raw) : {};
}

function toAgentError(error) {
  if (error instanceof AgentError) {
    return error;
  }
  if (error instanceof WorldAdapterError) {
    return worldAdapterErrorToAgentError(error);
  }
  return new AgentError(
    "EXECUTION_FAILED",
    error?.message || String(error)
  );
}

function protocolError(received) {
  return new AgentError(
    "UNSUPPORTED_PROTOCOL_VERSION",
    `Unsupported adapter_protocol_version: ${received}`,
    {
      expected: ADAPTER_PROTOCOL_VERSION,
      received: received ?? null,
    }
  );
}

function assertProtocol(body) {
  if (body.adapter_protocol_version !== ADAPTER_PROTOCOL_VERSION) {
    throw protocolError(body.adapter_protocol_version);
  }
}

function adapterInput(body, validate) {
  const input = clone(body);
  delete input.adapter_protocol_version;
  return validate(input);
}

function errorEnvelope(error, body = {}) {
  return {
    ok: false,
    adapter_protocol_version: ADAPTER_PROTOCOL_VERSION,
    request_id: body.request_id ?? null,
    session_id: body.session_id ?? null,
    world_id: body.world_id ?? null,
    error: error.toJSON(),
  };
}

function responseStatus(result) {
  return result.ok ? 200 : errorHttpStatus(result.error);
}

export async function startFakeMahoWorldServer({
  port = 0,
  auth_token = "",
  response_handler = null,
  capabilities: capability_overrides = {},
} = {}) {
  const tool_registry = createDefaultToolRegistry();
  const supported_tools = tool_registry
    .listDefinitions()
    .map((definition) => definition.name);
  const capabilities = {
    supports_atomic_transactions: true,
    supports_dry_run: true,
    supports_undo: true,
    supports_idempotency: true,
    max_tool_calls: 16,
    supported_tools,
    ...clone(capability_overrides),
  };
  const adapters = new Map();
  const requests = [];

  function adapterKey(session_id, world_id) {
    return `${session_id}\u0000${world_id}`;
  }

  function getAdapter(body) {
    const key = adapterKey(body.session_id, body.world_id);
    let adapter = adapters.get(key);
    if (!adapter) {
      adapter = new MockWorldAdapter({
        session_id: body.session_id,
        world_id: body.world_id,
        tool_registry,
        max_tool_calls: capabilities.max_tool_calls,
      });
      adapters.set(key, adapter);
    }
    return adapter;
  }

  async function defaultResponse(req, pathname, body) {
    if (
      auth_token &&
      req.headers.authorization !== `Bearer ${auth_token}`
    ) {
      return {
        status: 401,
        body: {
          ok: false,
          error: new AgentError(
            "PERMISSION_DENIED",
            "World adapter authentication failed"
          ).toJSON(),
        },
      };
    }

    if (
      req.method === "GET" &&
      pathname === "/world-adapter/v1/health"
    ) {
      return {
        status: 200,
        body: {
          ok: true,
          adapter_protocol_version: ADAPTER_PROTOCOL_VERSION,
          server_name: "fake-maho-world",
          server_version: "0.4-test",
          capabilities: clone(capabilities),
          error: null,
        },
      };
    }

    assertProtocol(body);
    if (
      req.method === "POST" &&
      pathname === "/world-adapter/v1/snapshot"
    ) {
      const input = adapterInput(body, validateGetSnapshotInput);
      const snapshot = await getAdapter(input).getSnapshot(input);
      return {
        status: 200,
        body: {
          ok: true,
          adapter_protocol_version: ADAPTER_PROTOCOL_VERSION,
          request_id: input.request_id,
          session_id: input.session_id,
          world_id: input.world_id,
          world_revision: snapshot.revision,
          timestamp_ms: Date.now(),
          capabilities: clone(capabilities),
          entities: snapshot.entities,
          history: snapshot.history,
          error: null,
        },
      };
    }

    if (
      req.method === "POST" &&
      pathname === "/world-adapter/v1/execute"
    ) {
      const input = adapterInput(
        body,
        validateExecuteTransactionInput
      );
      const result = await getAdapter(input).executeTransaction(input);
      return {
        status: responseStatus(result),
        body: {
          adapter_protocol_version: ADAPTER_PROTOCOL_VERSION,
          ...clone(result),
        },
      };
    }

    if (
      req.method === "POST" &&
      pathname === "/world-adapter/v1/undo"
    ) {
      const input = adapterInput(body, validateUndoInput);
      const result = await getAdapter(input).undo(input);
      return {
        status: responseStatus(result),
        body: {
          adapter_protocol_version: ADAPTER_PROTOCOL_VERSION,
          ...clone(result),
        },
      };
    }

    return {
      status: 404,
      body: {
        ok: false,
        error: new AgentError(
          "INVALID_REQUEST",
          "Fake world endpoint not found"
        ).toJSON(),
      },
    };
  }

  const server = http.createServer(async (req, res) => {
    const pathname = new URL(
      req.url || "/",
      "http://127.0.0.1"
    ).pathname;
    let body = {};
    try {
      if (req.method !== "GET") {
        body = await readJson(req);
      }
      const request = {
        method: req.method,
        pathname,
        body: clone(body),
        authorized:
          !auth_token ||
          req.headers.authorization === `Bearer ${auth_token}`,
      };
      requests.push(request);
      const fallback = await defaultResponse(req, pathname, body);
      const selected = response_handler
        ? (await response_handler({
            phase: pathname.split("/").at(-1),
            request: clone(request),
            request_count: requests.length,
            default_response: clone(fallback),
          })) || fallback
        : fallback;

      if (selected.delay_ms) {
        await new Promise((resolve) =>
          setTimeout(resolve, selected.delay_ms)
        );
      }
      if (selected.destroy) {
        res.destroy();
        return;
      }
      const status = selected.status ?? fallback.status;
      const payload =
        selected.raw !== undefined
          ? selected.raw
          : JSON.stringify(selected.body ?? fallback.body);
      res.writeHead(status, {
        "content-type": selected.content_type || "application/json",
      });
      res.end(payload);
    } catch (error) {
      const agent_error = toAgentError(error);
      if (!res.headersSent) {
        res.writeHead(errorHttpStatus(agent_error), {
          "content-type": "application/json",
        });
      }
      res.end(JSON.stringify(errorEnvelope(agent_error, body)));
    }
  });

  await new Promise((resolve, reject) => {
    server.once("error", reject);
    server.listen(port, "127.0.0.1", resolve);
  });
  const address = server.address();

  return {
    base_url: `http://127.0.0.1:${address.port}`,
    port: address.port,
    requests,
    adapters,
    capabilities: clone(capabilities),
    getAdapter(session_id, world_id) {
      return adapters.get(adapterKey(session_id, world_id)) || null;
    },
    async close() {
      server.closeAllConnections();
      if (server.listening) {
        await new Promise((resolve) => server.close(resolve));
      }
      await Promise.allSettled(
        [...adapters.values()].map((adapter) => adapter.close())
      );
      adapters.clear();
    },
  };
}
