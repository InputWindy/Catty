import { randomUUID } from "node:crypto";
import { readJson, RequestBodyTooLargeError } from "./body-parser.mjs";
import {
  AgentError,
  asAgentError,
  errorHttpStatus,
} from "../protocol/errors.mjs";
import { validateEnvelope } from "../protocol/envelope.mjs";
import { PROTOCOL_VERSION } from "../protocol/schemas.mjs";
import { sendAgentError, sendJson } from "./responses.mjs";
import {
  WorldAdapterError,
  worldAdapterErrorToAgentError,
} from "../world/world-adapter-errors.mjs";

export function createRouter({
  legacyChatService,
  session_manager,
  command_executor,
  agent_service,
  body_limit_bytes = 1024 * 1024,
  on_shutdown = () => {},
}) {
  return async function route(req, res) {
    try {
      const url = new URL(req.url || "/", "http://127.0.0.1");

      if (req.method === "GET" && url.pathname === "/health") {
        return sendJson(res, 200, legacyChatService.getHealth());
      }

      if (req.method === "GET" && url.pathname === "/events") {
        const after = Number(url.searchParams.get("after") ?? "-1");
        return sendJson(res, 200, legacyChatService.getEvents(after));
      }

      if (req.method === "POST" && url.pathname === "/shutdown") {
        sendJson(res, 200, { ok: true });
        on_shutdown();
        return;
      }

      if (req.method === "POST" && url.pathname === "/chat") {
        const body = await readJson(req, { limit_bytes: body_limit_bytes });
        const message = (body.message || "").trim();
        if (!message) {
          return sendJson(res, 400, { error: "message required" });
        }
        if (legacyChatService.isBusy()) {
          return sendJson(res, 409, { error: "Agent is busy" });
        }

        setImmediate(() => {
          legacyChatService.handleChat(message).catch((error) => {
            legacyChatService.handleUnhandledError(error);
          });
        });
        return sendJson(res, 200, { accepted: true });
      }

      if (req.method === "GET" && url.pathname === "/v1/health") {
        const provider_metadata = agent_service?.getProviderMetadata?.() || {};
        return sendJson(res, 200, {
          ok: true,
          protocol_version: PROTOCOL_VERSION,
          mock: legacyChatService.isMock(),
          busy: legacyChatService.isBusy(),
          status: legacyChatService.getHealth().status,
          provider: provider_metadata.provider ?? null,
          model: provider_metadata.model ?? null,
          provider_ready: provider_metadata.ready ?? false,
        });
      }

      if (req.method === "GET" && url.pathname === "/v1/events") {
        const after = Number(url.searchParams.get("after") ?? "-1");
        return sendJson(res, 200, {
          ok: true,
          protocol_version: PROTOCOL_VERSION,
          ...legacyChatService.getEvents(after),
        });
      }

      if (req.method === "POST" && url.pathname === "/v1/sessions") {
        requireCore(session_manager, "SessionManager");
        const body = await readJson(req, { limit_bytes: body_limit_bytes });
        if (
          body.protocol_version !== undefined &&
          body.protocol_version !== PROTOCOL_VERSION
        ) {
          throw new AgentError(
            "UNSUPPORTED_PROTOCOL_VERSION",
            `Unsupported protocol_version: ${body.protocol_version}`,
            { expected: PROTOCOL_VERSION, received: body.protocol_version }
          );
        }
        const session = session_manager.createSession();
        try {
          const snapshot = await session.adapter.getSnapshot({
            request_id: randomUUID(),
            session_id: session.session_id,
            world_id: session.world_id,
          });
          session.last_world_revision = snapshot.revision;
          return sendJson(res, 201, {
            ok: true,
            protocol_version: PROTOCOL_VERSION,
            session_id: session.session_id,
            world_id: session.world_id,
            world_revision: snapshot.revision,
            error: null,
          });
        } catch (error) {
          await session_manager.deleteSession(session.session_id);
          throw error;
        }
      }

      if (req.method === "POST" && url.pathname === "/v1/agent/run") {
        requireCore(agent_service, "AgentService");
        const body = await readJson(req, { limit_bytes: body_limit_bytes });
        const controller = new AbortController();
        const cancel = () =>
          controller.abort(new Error("HTTP client disconnected"));
        req.once("aborted", cancel);
        res.once("close", () => {
          if (!res.writableEnded) {
            cancel();
          }
        });
        const result = await agent_service.run({
          protocol_version: body.protocol_version || PROTOCOL_VERSION,
          request_id: body.request_id,
          session_id: body.session_id,
          world_id: body.world_id,
          message: body.message,
          expected_revision: body.expected_revision,
          signal: controller.signal,
        });
        if (!result.replayed && result.ok && result.assistant_message) {
          legacyChatService.pushEvent("assistant", result.assistant_message);
        } else if (!result.replayed && !result.ok) {
          legacyChatService.pushEvent("system", result.assistant_message);
        }
        return sendJson(
          res,
          result.ok ? 200 : errorHttpStatus(result.error),
          result
        );
      }

      if (req.method === "POST" && url.pathname === "/v1/tools/execute") {
        requireCore(command_executor, "CommandExecutor");
        requireCore(session_manager, "SessionManager");
        const body = await readJson(req, { limit_bytes: body_limit_bytes });
        let request;
        if (body.payload !== undefined) {
          validateEnvelope(body);
          if (body.type !== "tools.execute") {
            throw new AgentError(
              "INVALID_REQUEST",
              "Envelope type must be tools.execute",
              { received: body.type }
            );
          }
          request = {
            protocol_version: body.protocol_version,
            request_id: body.request_id,
            session_id: body.session_id,
            world_id: body.world_id,
            tool_calls: body.payload.tool_calls,
          };
        } else {
          const session = session_manager.getSession(body.session_id);
          request = {
            protocol_version: body.protocol_version || PROTOCOL_VERSION,
            request_id: body.request_id,
            session_id: body.session_id,
            world_id: body.world_id || session.world_id,
            tool_calls: body.tool_calls,
          };
        }
        const result = await command_executor.executeBatch(request);
        return sendJson(
          res,
          result.ok ? 200 : errorHttpStatus(result.error),
          result
        );
      }

      if (req.method === "GET" && url.pathname === "/v1/world/snapshot") {
        requireCore(session_manager, "SessionManager");
        const session_id = url.searchParams.get("session_id");
        if (!session_id) {
          throw new AgentError(
            "INVALID_REQUEST",
            "session_id query parameter is required",
            { field: "session_id" }
          );
        }
        const session = session_manager.getSession(session_id);
        const world_id =
          url.searchParams.get("world_id") || session.world_id;
        const adapter = session_manager.getAdapter(session_id, world_id);
        const snapshot = await adapter.getSnapshot({
          request_id: randomUUID(),
          session_id,
          world_id,
        });
        session.last_world_revision = snapshot.revision;
        return sendJson(res, 200, {
          ok: true,
          protocol_version: PROTOCOL_VERSION,
          snapshot,
          error: null,
        });
      }

      if (req.method === "POST" && url.pathname === "/v1/history/undo") {
        requireCore(command_executor, "CommandExecutor");
        requireCore(session_manager, "SessionManager");
        const body = await readJson(req, { limit_bytes: body_limit_bytes });
        const session = session_manager.getSession(body.session_id);
        const request = {
          protocol_version: body.protocol_version || PROTOCOL_VERSION,
          request_id: body.request_id,
          session_id: body.session_id,
          world_id: body.world_id || session.world_id,
          tool_calls: [
            {
              tool_call_id: body.tool_call_id || randomUUID(),
              tool_name: "history.undo",
              expected_revision: body.expected_revision,
              dry_run: body.dry_run ?? false,
              args: body.undo_token ? { undo_token: body.undo_token } : {},
            },
          ],
        };
        const result = await command_executor.executeBatch(request);
        return sendJson(
          res,
          result.ok ? 200 : errorHttpStatus(result.error),
          result
        );
      }

      return sendJson(res, 404, { error: "not found" });
    } catch (error) {
      if (error instanceof RequestBodyTooLargeError) {
        const url = new URL(req.url || "/", "http://127.0.0.1");
        if (url.pathname.startsWith("/v1/")) {
          return sendAgentError(
            res,
            new AgentError("REQUEST_TOO_LARGE", error.message, {
              limit_bytes: error.limit_bytes,
            }),
            413
          );
        }
        return sendJson(res, 413, { error: error.message });
      }
      if (error instanceof AgentError) {
        return sendAgentError(res, error, errorHttpStatus(error));
      }
      if (error instanceof WorldAdapterError) {
        const agent_error = worldAdapterErrorToAgentError(error);
        return sendAgentError(
          res,
          agent_error,
          errorHttpStatus(agent_error)
        );
      }
      const agent_error = asAgentError(error);
      const url = new URL(req.url || "/", "http://127.0.0.1");
      if (url.pathname.startsWith("/v1/")) {
        return sendAgentError(res, agent_error, errorHttpStatus(agent_error));
      }
      return sendJson(res, 500, { error: agent_error.message });
    }
  };
}

function requireCore(value, name) {
  if (!value) {
    throw new AgentError("INTERNAL_ERROR", `${name} is not configured`);
  }
}
