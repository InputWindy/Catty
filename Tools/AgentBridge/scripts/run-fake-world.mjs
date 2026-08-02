import { pathToFileURL } from "node:url";
import { startFakeMahoWorldServer } from "../tests/helpers/fake-maho-world-server.mjs";

function parsePort(value) {
  const port = Number(value);
  if (!Number.isInteger(port) || port < 0 || port > 65535) {
    throw new Error("MAHO_WORLD_FAKE_PORT must be an integer from 0 through 65535");
  }
  return port;
}

function parseProfile(value) {
  const profile = String(value || "full").trim().toLowerCase();
  if (!new Set(["full", "minimal"]).has(profile)) {
    throw new Error("MAHO_WORLD_FAKE_PROFILE must be full or minimal");
  }
  return profile;
}

export async function main({
  env = process.env,
  output = process.stdout,
  error_output = process.stderr,
} = {}) {
  let fake;
  try {
    const port = parsePort(env.MAHO_WORLD_FAKE_PORT || 8770);
    const auth_token = String(
      env.MAHO_WORLD_AUTH_TOKEN || ""
    ).trim();
    const profile = parseProfile(env.MAHO_WORLD_FAKE_PROFILE);
    fake = await startFakeMahoWorldServer({
      port,
      auth_token,
      profile,
    });
    output.write(
      `FakeMahoWorldServer listening on ${fake.base_url} adapter_protocol_version=1.0 profile=${profile} auth=${auth_token ? "required" : "disabled"}\n`
    );
    await new Promise((resolve) => {
      const stop = () => resolve();
      process.once("SIGINT", stop);
      process.once("SIGTERM", stop);
    });
    return 0;
  } catch (error) {
    error_output.write(
      `FakeMahoWorldServer failed: ${error?.message || String(error)}\n`
    );
    return 1;
  } finally {
    await fake?.close();
  }
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  process.exitCode = await main();
}
