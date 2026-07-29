/**
 * Catty editor Agent bridge — local HTTP sidecar around @cursor/sdk.
 *
 *   node server.mjs --port 8765 --cwd <projectRoot> [--api-key-file path]
 *
 * Api key resolution order:
 *   1) --api-key-file
 *   2) --api-key
 *   3) CURSOR_API_KEY env
 *
 * CATTY_AGENT_MOCK=1 forces mock replies.
 */

import fs from "node:fs";
import path from "node:path";
import http from "node:http";
import { Agent, JsonlLocalAgentStore } from "@cursor/sdk";

const SYSTEM_PREAMBLE =
  "You are the Catty engine in-editor Agent. Help the user write and refine Lua " +
  "game scripts for Catty (catty.log / cvars / packages / resources, Scripts/*.lua). " +
  "Do not rewrite engine C++ unless explicitly asked. Prefer complete, runnable Lua. " +
  "Keep answers concise unless the user asks for detail.\n\nUser message:\n";

function parseArgs(argv) {
  const Out = { port: 8765, cwd: process.cwd(), apiKey: "", apiKeyFile: "" };
  for (let i = 2; i < argv.length; ++i) {
    const Arg = argv[i];
    if (Arg === "--port" && argv[i + 1]) {
      Out.port = Number(argv[++i]);
    } else if (Arg === "--cwd" && argv[i + 1]) {
      Out.cwd = argv[++i];
    } else if (Arg === "--api-key" && argv[i + 1]) {
      Out.apiKey = String(argv[++i] || "").trim();
    } else if (Arg === "--api-key-file" && argv[i + 1]) {
      Out.apiKeyFile = String(argv[++i] || "").trim();
    }
  }
  return Out;
}

function resolveApiKey(Args) {
  if (Args.apiKey) {
    return Args.apiKey;
  }
  if (Args.apiKeyFile) {
    try {
      return fs.readFileSync(Args.apiKeyFile, "utf8").trim();
    } catch (Err) {
      console.error("[CattyAgentBridge] failed to read api-key-file:", Err.message || Err);
    }
  }
  return (process.env.CURSOR_API_KEY || "").trim();
}

const Args = parseArgs(process.argv);
const ApiKey = resolveApiKey(Args);
const ForceMock = process.env.CATTY_AGENT_MOCK === "1" || !ApiKey;

let agent = null;
let busy = false;
let status = ForceMock
  ? "mock (no CURSOR_API_KEY)"
  : "starting";
let nextId = 0;
const events = [];

console.log(
  `[CattyAgentBridge] apiKey=${ApiKey ? `present(len=${ApiKey.length})` : "missing"} mock=${ForceMock}`
);
function pushEvent(role, text) {
  const Event = { id: nextId++, role, text: String(text ?? "") };
  events.push(Event);
  if (events.length > 500) {
    events.splice(0, events.length - 500);
  }
  return Event;
}

function readJson(req) {
  return new Promise((resolve, reject) => {
    const Chunks = [];
    req.on("data", (c) => Chunks.push(c));
    req.on("end", () => {
      try {
        const Raw = Buffer.concat(Chunks).toString("utf8");
        resolve(Raw ? JSON.parse(Raw) : {});
      } catch (Err) {
        reject(Err);
      }
    });
    req.on("error", reject);
  });
}

function sendJson(res, code, obj) {
  const Body = JSON.stringify(obj);
  res.writeHead(code, {
    "Content-Type": "application/json; charset=utf-8",
    "Content-Length": Buffer.byteLength(Body),
  });
  res.end(Body);
}

async function handleChat(message) {
  if (busy) {
    return { error: "Agent is busy" };
  }
  busy = true;
  const Mock = ForceMock || !agent;
  status = Mock ? "mock thinking..." : "thinking...";
  try {
    if (Mock) {
      await new Promise((r) => setTimeout(r, 400));
      const Reply =
        "(Mock Agent — set CURSOR_API_KEY for a real Cursor Agent)\n\n" +
        "You said:\n> " +
        message +
        "\n\nExample Lua:\n```lua\ncatty.log(\"hello from Agent\")\n```";
      pushEvent("assistant", Reply);
      status = ForceMock ? "mock (no CURSOR_API_KEY)" : "mock fallback";
      return { ok: true };
    }

    const Prompt = SYSTEM_PREAMBLE + message;
    const Run = await agent.send(Prompt);
    let Text = "";
    for await (const Event of Run.stream()) {
      if (Event.type === "assistant" && Event.message && Event.message.content) {
        for (const Block of Event.message.content) {
          if (Block.type === "text" && Block.text) {
            Text += Block.text;
          }
        }
      }
    }
    await Run.wait();
    if (!Text.trim()) {
      Text = "(Agent finished with empty text.)";
    }
    pushEvent("assistant", Text);
    status = "ready";
    return { ok: true };
  } catch (Err) {
    const Msg = Err && Err.message ? Err.message : String(Err);
    pushEvent("system", "Agent error: " + Msg);
    status = "error";
    return { error: Msg };
  } finally {
    busy = false;
  }
}

async function main() {
  pushEvent(
    "system",
    ForceMock
      ? "Agent bridge online in MOCK mode. Set CURSOR_API_KEY and restart the engine for a real Cursor Agent."
      : "Agent bridge starting Cursor local Agent..."
  );

  if (!ForceMock) {
    try {
      const StoreRoot = path.join(Args.cwd, "Saved", "Agent", "cursor-sdk-store");
      fs.mkdirSync(StoreRoot, { recursive: true });
      // Node < 22.13 has no node:sqlite; JSONL store keeps local agents working.
      const Store = new JsonlLocalAgentStore(StoreRoot);
      agent = await Agent.create({
        apiKey: ApiKey,
        model: { id: "composer-2.5" },
        local: { cwd: Args.cwd, store: Store },
      });
      status = "ready";
      pushEvent("system", "Cursor local Agent ready. cwd=" + Args.cwd);
    } catch (Err) {
      const Msg = Err && Err.message ? Err.message : String(Err);
      status = "sdk failed — mock fallback";
      pushEvent("system", "Agent.create failed (" + Msg + "). Falling back to mock replies.");
      // Keep serving mock so the panel still works.
      agent = null;
    }
  }

  const UseMock = () => ForceMock || !agent;

  const Server = http.createServer(async (req, res) => {
    try {
      const Url = new URL(req.url || "/", "http://127.0.0.1");
      if (req.method === "GET" && Url.pathname === "/health") {
        return sendJson(res, 200, {
          ok: true,
          mock: UseMock(),
          busy,
          status,
          cwd: Args.cwd,
        });
      }
      if (req.method === "GET" && Url.pathname === "/events") {
        const After = Number(Url.searchParams.get("after") ?? "-1");
        const Slice = events.filter((e) => e.id > After);
        return sendJson(res, 200, {
          events: Slice,
          busy,
          mock: UseMock(),
          status,
        });
      }
      if (req.method === "POST" && Url.pathname === "/shutdown") {
        sendJson(res, 200, { ok: true });
        setTimeout(() => process.exit(0), 50);
        return;
      }
      if (req.method === "POST" && Url.pathname === "/chat") {
        const Body = await readJson(req);
        const Message = (Body.message || "").trim();
        if (!Message) {
          return sendJson(res, 400, { error: "message required" });
        }
        if (busy) {
          return sendJson(res, 409, { error: "Agent is busy" });
        }
        // Acknowledge immediately; reply arrives via /events.
        setImmediate(() => {
          handleChat(Message).catch((Err) => {
            pushEvent("system", "Unhandled chat error: " + Err);
            busy = false;
          });
        });
        return sendJson(res, 200, { accepted: true });
      }
      return sendJson(res, 404, { error: "not found" });
    } catch (Err) {
      return sendJson(res, 500, { error: String(Err && Err.message ? Err.message : Err) });
    }
  });

  Server.listen(Args.port, "127.0.0.1", () => {
    console.log(
      `[CattyAgentBridge] listening on 127.0.0.1:${Args.port} cwd=${Args.cwd} mock=${UseMock()}`
    );
  });

  const Shutdown = async () => {
    try {
      if (agent && typeof agent[Symbol.asyncDispose] === "function") {
        await agent[Symbol.asyncDispose]();
      } else if (agent && typeof agent.close === "function") {
        await agent.close();
      }
    } catch (_) {
      /* ignore */
    }
    process.exit(0);
  };
  process.on("SIGINT", Shutdown);
  process.on("SIGTERM", Shutdown);
}

main().catch((Err) => {
  console.error("[CattyAgentBridge] fatal", Err);
  process.exit(1);
});
