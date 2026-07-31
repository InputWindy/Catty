import http from "node:http";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { createRouter } from "../../src/api/router.mjs";
import { AgentService } from "../../src/agent/agent-service.mjs";
import { LegacyChatService } from "../../src/agent/legacy-chat-service.mjs";
import { MockProvider } from "../../src/agent/providers/mock-provider.mjs";
import { CommandExecutor } from "../../src/execution/command-executor.mjs";
import { AuditLog } from "../../src/logging/audit-log.mjs";
import { SessionManager } from "../../src/sessions/session-manager.mjs";
import { createDefaultToolRegistry } from "../../src/tools/definitions.mjs";
import {
  WorldAdapterFactory,
  worldAdapterConfigDefaults,
} from "../../src/world/world-adapter-factory.mjs";

export async function startLegacyTestServer(overrides = {}) {
  const temporary_dir = await fs.mkdtemp(
    path.join(os.tmpdir(), "maho-agent-test-")
  );
  const config = {
    host: "127.0.0.1",
    port: 0,
    cwd: process.cwd(),
    api_key: "",
    force_mock: true,
    data_dir: temporary_dir,
    body_limit_bytes: 1024 * 1024,
    ...overrides,
  };
  const legacyChatService = new LegacyChatService(config);
  await legacyChatService.initialize();
  const tool_registry = createDefaultToolRegistry();
  const world_adapter_factory = new WorldAdapterFactory({
    config: overrides.world_config || {
      ...worldAdapterConfigDefaults,
    },
    tool_registry,
  });
  const session_manager = new SessionManager({
    world_adapter_factory,
  });
  const audit_log = new AuditLog({ data_dir: config.data_dir });
  const command_executor = new CommandExecutor({
    session_manager,
    tool_registry,
    audit_log,
  });
  const provider = overrides.provider || new MockProvider();
  const agent_service = new AgentService({
    session_manager,
    tool_registry,
    command_executor,
    provider,
    audit_log,
  });

  let shutdown_requested = false;
  const server = http.createServer(
    createRouter({
      legacyChatService,
      session_manager,
      command_executor,
      agent_service,
      body_limit_bytes: config.body_limit_bytes,
      on_shutdown: () => {
        shutdown_requested = true;
        server.close();
      },
    })
  );
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const address = server.address();

  return {
    base_url: `http://127.0.0.1:${address.port}`,
    legacyChatService,
    session_manager,
    tool_registry,
    world_adapter_factory,
    command_executor,
    agent_service,
    audit_log,
    temporary_dir,
    wasShutdownRequested: () => shutdown_requested,
    async close() {
      if (server.listening) {
        await new Promise((resolve) => server.close(resolve));
      }
      await legacyChatService.close();
      await session_manager.close();
      await fs.rm(temporary_dir, { recursive: true, force: true });
    },
  };
}

export async function postJson(base_url, pathname, body) {
  const response = await fetch(`${base_url}${pathname}`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(body),
  });
  return {
    status: response.status,
    body: await response.json(),
  };
}
