import { pathToFileURL } from "node:url";
import {
  WorldAdapterFactory,
  resolveWorldAdapterConfig,
} from "../src/world/world-adapter-factory.mjs";
import { startFakeMahoWorldServer } from "../tests/helpers/fake-maho-world-server.mjs";
import { runEvaluations } from "./eval-runner.mjs";

export async function runRemoteEvaluations({
  output = process.stdout,
  error_output = process.stderr,
  profile = "full",
  case_files,
  adapter_name = profile === "minimal" ? "remote:minimal" : "remote",
} = {}) {
  const fake = await startFakeMahoWorldServer({ profile });
  try {
    const config = resolveWorldAdapterConfig({
      env: {
        MAHO_WORLD_ADAPTER: "remote",
        MAHO_WORLD_BASE_URL: fake.base_url,
      },
    });
    const summary = await runEvaluations({
      output,
      adapter_name,
      case_files,
      create_world_adapter_factory(tool_registry) {
        return new WorldAdapterFactory({
          config,
          tool_registry,
        });
      },
      async seed_session(session, initial_entities) {
        if (initial_entities.length === 0) {
          return;
        }
        const backing = fake.getAdapter(
          session.session_id,
          session.world_id
        );
        if (!backing) {
          throw new Error("Fake world Session was not initialized");
        }
        for (const entity of initial_entities) {
          backing.mock_world.spawnPrimitive(entity);
        }
      },
    });
    return summary.failed === 0 ? 0 : 1;
  } catch (error) {
    error_output.write(
      `Remote eval runner failed: ${error?.stack || error}\n`
    );
    return 1;
  } finally {
    await fake.close();
  }
}

export async function main(options = {}) {
  return runRemoteEvaluations(options);
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  process.exitCode = await main();
}
