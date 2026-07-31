/**
 * Maho editor Agent bridge.
 *
 *   node server.mjs --port 8765 --cwd <projectRoot> [--api-key-file path]
 */

import http from "node:http";
import { loadConfig } from "./src/config.mjs";
import { createRouter } from "./src/api/router.mjs";
import { AgentService } from "./src/agent/agent-service.mjs";
import { LegacyChatService } from "./src/agent/legacy-chat-service.mjs";
import { ProviderRegistry } from "./src/agent/provider-registry.mjs";
import { CommandExecutor } from "./src/execution/command-executor.mjs";
import { AuditLog } from "./src/logging/audit-log.mjs";
import { SessionManager } from "./src/sessions/session-manager.mjs";
import { createDefaultToolRegistry } from "./src/tools/definitions.mjs";
import { WorldAdapterFactory } from "./src/world/world-adapter-factory.mjs";

const config = loadConfig();
const providerRegistry = new ProviderRegistry({ config: config.ai });
const provider = await providerRegistry.initialize();
const legacyChatService = new LegacyChatService(config, {
  agent: providerRegistry.getLegacyAgent(),
});
await legacyChatService.initialize();
const toolRegistry = createDefaultToolRegistry();
const worldAdapterFactory = new WorldAdapterFactory({
  config: config.world,
  tool_registry: toolRegistry,
});
const sessionManager = new SessionManager({
  world_adapter_factory: worldAdapterFactory,
});
const auditLog = new AuditLog({ data_dir: config.data_dir });
const commandExecutor = new CommandExecutor({
  session_manager: sessionManager,
  tool_registry: toolRegistry,
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
  await providerRegistry.close();
  await sessionManager.close();

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
        console.error("[MahoAgentBridge] shutdown failed", error);
        process.exit(1);
      });
    }, 50);
  },
});

server = http.createServer(router);
server.listen(config.port, config.host, () => {
  const provider_metadata = providerRegistry.getMetadata();
  const world_metadata = worldAdapterFactory.getMetadata();
  console.log(
    `[MahoAgentBridge] listening on ${config.host}:${config.port} cwd=${config.cwd} provider=${provider_metadata.provider} model=${provider_metadata.model} mode=${provider_metadata.real ? "real" : "mock"} thinking=${provider_metadata.thinking} world_adapter=${world_metadata.adapter}${world_metadata.base_url ? ` world_base_url=${world_metadata.base_url}` : ""}`
  );
});

process.on("SIGINT", () => {
  shutdown().catch(() => process.exit(1));
});
process.on("SIGTERM", () => {
  shutdown().catch(() => process.exit(1));
});
