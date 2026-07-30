import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { resolveProviderConfig } from "./agent/provider-config.mjs";

const DEFAULT_PORT = 8765;
const DEFAULT_BODY_LIMIT_BYTES = 1024 * 1024;
const BRIDGE_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");

function parseArgs(argv) {
  const parsed = {
    port: undefined,
    cwd: undefined,
    api_key: "",
    api_key_file: "",
  };

  for (let index = 2; index < argv.length; index += 1) {
    const argument = argv[index];
    if (argument === "--port" && argv[index + 1]) {
      parsed.port = Number(argv[++index]);
    } else if (argument === "--cwd" && argv[index + 1]) {
      parsed.cwd = argv[++index];
    } else if (argument === "--api-key" && argv[index + 1]) {
      parsed.api_key = String(argv[++index] || "").trim();
    } else if (argument === "--api-key-file" && argv[index + 1]) {
      parsed.api_key_file = String(argv[++index] || "").trim();
    }
  }

  return parsed;
}

function parsePort(value) {
  const port = Number(value);
  if (!Number.isInteger(port) || port < 0 || port > 65535) {
    throw new Error(`Invalid AgentBridge port: ${value}`);
  }
  return port;
}

function resolveApiKey(args, logger, env) {
  if (args.api_key) {
    return args.api_key;
  }
  if (args.api_key_file) {
    try {
      return fs.readFileSync(args.api_key_file, "utf8").trim();
    } catch (error) {
      logger.error(
        "[CattyAgentBridge] failed to read api-key-file:",
        error?.message || error
      );
    }
  }
  return (env.CURSOR_API_KEY || "").trim();
}

export function loadConfig({
  argv = process.argv,
  env = process.env,
  cwd = process.cwd(),
  logger = console,
} = {}) {
  const args = parseArgs(argv);
  const api_key = resolveApiKey(args, logger, env);
  const host = String(env.CATTY_AGENT_HOST || "127.0.0.1").trim();
  const port = parsePort(args.port ?? env.CATTY_AGENT_PORT ?? DEFAULT_PORT);
  const resolved_cwd = path.resolve(args.cwd || cwd);
  const ai = resolveProviderConfig({
    env,
    legacy_api_key: api_key,
    cwd: resolved_cwd,
  });

  if (!["127.0.0.1", "::1"].includes(host)) {
    throw new Error("CATTY_AGENT_HOST must resolve to a loopback-only host");
  }

  return {
    host,
    port,
    cwd: resolved_cwd,
    api_key,
    force_mock: ai.provider_id === "mock",
    ai,
    data_dir: path.resolve(env.CATTY_AGENT_DATA_DIR || path.join(BRIDGE_ROOT, ".runtime")),
    body_limit_bytes: DEFAULT_BODY_LIMIT_BYTES,
  };
}

export const configDefaults = Object.freeze({
  host: "127.0.0.1",
  port: DEFAULT_PORT,
  body_limit_bytes: DEFAULT_BODY_LIMIT_BYTES,
});
