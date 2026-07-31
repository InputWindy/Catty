import { pathToFileURL } from "node:url";
import { startFakeCattyWorldServer } from "../tests/helpers/fake-catty-world-server.mjs";

function parsePort(value) {
  const port = Number(value);
  if (!Number.isInteger(port) || port < 0 || port > 65535) {
    throw new Error("CATTY_WORLD_FAKE_PORT must be an integer from 0 through 65535");
  }
  return port;
}

export async function main({
  env = process.env,
  output = process.stdout,
  error_output = process.stderr,
} = {}) {
  let fake;
  try {
    const port = parsePort(env.CATTY_WORLD_FAKE_PORT || 8770);
    const auth_token = String(
      env.CATTY_WORLD_AUTH_TOKEN || ""
    ).trim();
    fake = await startFakeCattyWorldServer({
      port,
      auth_token,
    });
    output.write(
      `FakeCattyWorldServer listening on ${fake.base_url} adapter_protocol_version=1.0 auth=${auth_token ? "required" : "disabled"}\n`
    );
    await new Promise((resolve) => {
      const stop = () => resolve();
      process.once("SIGINT", stop);
      process.once("SIGTERM", stop);
    });
    return 0;
  } catch (error) {
    error_output.write(
      `FakeCattyWorldServer failed: ${error?.message || String(error)}\n`
    );
    return 1;
  } finally {
    await fake?.close();
  }
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  process.exitCode = await main();
}
