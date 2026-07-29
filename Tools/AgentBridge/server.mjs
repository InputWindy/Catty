/**
 * Catty editor Agent bridge.
 *
 *   node server.mjs --port 8765 --cwd <projectRoot> [--api-key-file path]
 */

import http from "node:http";
import { loadConfig } from "./src/config.mjs";
import { createRouter } from "./src/api/router.mjs";
import { AgentService } from "./src/agent/agent-service.mjs";
import { LegacyChatService } from "./src/agent/legacy-chat-service.mjs";
import { CursorProvider } from "./src/agent/providers/cursor-provider.mjs";
import { MockProvider } from "./src/agent/providers/mock-provider.mjs";
import { CommandExecutor } from "./src/execution/command-executor.mjs";
import { UndoJournal } from "./src/history/undo-journal.mjs";
import { AuditLog } from "./src/logging/audit-log.mjs";
import { SessionManager } from "./src/sessions/session-manager.mjs";
import { createDefaultToolRegistry } from "./src/tools/definitions.mjs";

const config = loadConfig();
const legacyChatService = new LegacyChatService(config);
await legacyChatService.initialize();
const sessionManager = new SessionManager();
const toolRegistry = createDefaultToolRegistry();
const undoJournal = new UndoJournal();
const auditLog = new AuditLog({ data_dir: config.data_dir });
const provider = legacyChatService.isMock()
  ? new MockProvider()
  : new CursorProvider({ agent: legacyChatService.getAgent() });
const commandExecutor = new CommandExecutor({
  session_manager: sessionManager,
  tool_registry: toolRegistry,
  undo_journal: undoJournal,
  audit_log: auditLog,
});
const agentService = new AgentService({
  session_manager: sessionManager,
  tool_registry: toolRegistry,
  command_executor: commandExecutor,
  provider,
  audit_log: auditLog,
});

let shuttingDown = false;
let server;

async function shutdown({ exit = true } = {}) {
  if (shuttingDown) {
    return;
  }
  shuttingDown = true;

  await new Promise((resolve) => {
    if (!server?.listening) {
      resolve();
      return;
    }
    server.close(resolve);
  });
  await legacyChatService.close();

  if (exit) {
    process.exit(0);
  }
}

const router = createRouter({
  legacyChatService,
  session_manager: sessionManager,
  command_executor: commandExecutor,
  agent_service: agentService,
  body_limit_bytes: config.body_limit_bytes,
  on_shutdown: () => {
    setTimeout(() => {
      shutdown().catch((error) => {
        console.error("[CattyAgentBridge] shutdown failed", error);
        process.exit(1);
      });
    }, 50);
  },
});

server = http.createServer(router);
server.listen(config.port, config.host, () => {
  console.log(
    `[CattyAgentBridge] listening on ${config.host}:${config.port} cwd=${config.cwd} mock=${legacyChatService.isMock()}`
  );
});

process.on("SIGINT", () => {
  shutdown().catch(() => process.exit(1));
});
process.on("SIGTERM", () => {
  shutdown().catch(() => process.exit(1));
});
