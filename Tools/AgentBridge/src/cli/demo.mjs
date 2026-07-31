import { randomUUID } from "node:crypto";
import readline from "node:readline";
import { pathToFileURL } from "node:url";
import { AgentService } from "../agent/agent-service.mjs";
import { resolveProviderConfig } from "../agent/provider-config.mjs";
import { ProviderRegistry } from "../agent/provider-registry.mjs";
import { MockProvider } from "../agent/providers/mock-provider.mjs";
import { CommandExecutor } from "../execution/command-executor.mjs";
import { UndoJournal } from "../history/undo-journal.mjs";
import { NullAuditLog } from "../logging/audit-log.mjs";
import { SessionManager } from "../sessions/session-manager.mjs";
import { createDefaultToolRegistry } from "../tools/definitions.mjs";

const HELP_TEXT = `Commands:
  /help      Show this help
  /world     Print the complete world snapshot
  /entities  List entities
  /undo      Undo the latest successful world change
  /reset     Start a new independent Session
  /exit      Exit the demo`;

class RecordingProvider {
  constructor(provider) {
    this.name = provider.name;
    this.model = provider.model;
    this.capabilities = provider.capabilities;
    this.finalization_enabled = provider.finalization_enabled;
    this.provider = provider;
    this.last_output = null;
  }

  getMetadata() {
    return this.provider.getMetadata();
  }

  async plan(input) {
    this.last_output = await this.provider.plan(input);
    return this.last_output;
  }

  async finalize(input) {
    return this.provider.finalize(input);
  }
}

export function createDemoState({
  provider: selected_provider = new MockProvider(),
  provider_registry = null,
} = {}) {
  const session_manager = new SessionManager();
  const tool_registry = createDefaultToolRegistry();
  const undo_journal = new UndoJournal();
  const audit_log = new NullAuditLog();
  const command_executor = new CommandExecutor({
    session_manager,
    tool_registry,
    undo_journal,
    audit_log,
  });
  const provider = new RecordingProvider(selected_provider);
  const agent_service = new AgentService({
    session_manager,
    tool_registry,
    command_executor,
    provider,
    provider_registry,
    audit_log,
  });
  return {
    session_manager,
    tool_registry,
    undo_journal,
    command_executor,
    provider,
    agent_service,
    active_session: session_manager.createSession(),
  };
}

export async function createConfiguredDemoState({
  env = process.env,
  cwd = process.cwd(),
} = {}) {
  const config = resolveProviderConfig({ env, cwd });
  const provider_registry = new ProviderRegistry({ config });
  const selected_provider = await provider_registry.initialize();
  return createDemoState({
    provider: selected_provider,
    provider_registry,
  });
}

export function formatWorld(snapshot) {
  return JSON.stringify(snapshot, null, 2);
}

export function formatEntities(snapshot) {
  if (snapshot.entities.length === 0) {
    return "Entities: (none)";
  }
  return [
    `Entities: ${snapshot.entities.length}`,
    ...snapshot.entities.map(
      (entity) =>
        `  - ${entity.name} [${entity.primitive_type}] ${entity.entity_id}`
    ),
  ].join("\n");
}

export function formatAgentResult(result, provider_output, snapshot) {
  const result_by_call_id = new Map(
    result.tool_results.map((tool_result) => [
      tool_result.tool_call_id,
      tool_result,
    ])
  );
  const tool_lines = provider_output?.tool_calls?.length
    ? provider_output.tool_calls.map((tool_call) => {
        const tool_result = result_by_call_id.get(tool_call.tool_call_id);
        return `  ${tool_call.tool_name} ${tool_result?.ok ? "✓" : "✗"}`;
      })
    : ["  (none)"];
  return [
    `Agent: ${result.assistant_message}`,
    "Tools:",
    ...tool_lines,
    `World revision: ${result.world_revision}`,
    `Entities: ${snapshot.entities.length}`,
  ].join("\n");
}

async function runAgent(state, message) {
  const session = state.active_session;
  state.provider.last_output = null;
  const result = await state.agent_service.run({
    protocol_version: "1.0",
    request_id: randomUUID(),
    session_id: session.session_id,
    message,
    expected_revision: session.world.revision,
  });
  return formatAgentResult(
    result,
    state.provider.last_output,
    session.world.snapshot()
  );
}

export async function handleDemoInput(state, input) {
  const line = input.trim();
  if (!line) {
    return { exit: false, output: "" };
  }

  try {
    const command = line.toLowerCase();
    if (command === "/help") {
      return { exit: false, output: HELP_TEXT };
    }
    if (command === "/world") {
      return {
        exit: false,
        output: formatWorld(state.active_session.world.snapshot()),
      };
    }
    if (command === "/entities") {
      return {
        exit: false,
        output: formatEntities(state.active_session.world.snapshot()),
      };
    }
    if (command === "/undo") {
      return {
        exit: false,
        output: await runAgent(state, "undo the last action"),
      };
    }
    if (command === "/reset") {
      state.active_session = state.session_manager.createSession();
      return {
        exit: false,
        output: `Started new Session: ${state.active_session.session_id}`,
      };
    }
    if (command === "/exit") {
      return { exit: true, output: "Bye." };
    }
    if (command.startsWith("/")) {
      return {
        exit: false,
        output: `Unknown command: ${line}\nType /help for commands.`,
      };
    }
    return {
      exit: false,
      output: await runAgent(state, line),
    };
  } catch (error) {
    return {
      exit: false,
      output: `Error: ${error?.message || String(error)}`,
    };
  }
}

export async function runDemo({
  input = process.stdin,
  output = process.stdout,
  env = process.env,
  cwd = process.cwd(),
} = {}) {
  let state;
  try {
    state = await createConfiguredDemoState({ env, cwd });
  } catch (error) {
    output.write(`Provider configuration error: ${error?.message || String(error)}\n`);
    return 1;
  }
  const interactive = Boolean(input.isTTY && output.isTTY);
  const reader = readline.createInterface({
    input,
    output: interactive ? output : undefined,
    terminal: interactive,
    crlfDelay: Infinity,
  });

  const metadata = state.provider.getMetadata();
  output.write(
    `Catty Agent Core v0.3\nProvider: ${metadata.provider}\nModel: ${metadata.model}\nMode: ${metadata.real ? "real" : "Mock"}\nThinking: disabled\nType /help for commands.\n\n`
  );
  reader.on("SIGINT", () => {
    output.write("\nBye.\n");
    reader.close();
  });

  if (interactive) {
    output.write("> ");
  }
  try {
    for await (const line of reader) {
      const handled = await handleDemoInput(state, line);
      if (handled.output) {
        output.write(`${handled.output}\n`);
      }
      if (handled.exit) {
        break;
      }
      if (interactive) {
        output.write("\n> ");
      }
    }
  } catch (error) {
    output.write(`Error: ${error?.message || String(error)}\n`);
    return 1;
  } finally {
    reader.close();
    await state.provider_registry?.close();
  }
  return 0;
}

if (
  process.argv[1] &&
  import.meta.url === pathToFileURL(process.argv[1]).href
) {
  process.exitCode = await runDemo();
}
