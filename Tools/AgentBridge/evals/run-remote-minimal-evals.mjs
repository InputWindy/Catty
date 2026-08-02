import { pathToFileURL } from "node:url";
import { runRemoteEvaluations } from "./run-remote-evals.mjs";

const MINIMAL_CASES = [
  new URL("./cases-minimal/minimal-world-profile.json", import.meta.url),
];

export async function main(options = {}) {
  return runRemoteEvaluations({
    ...options,
    profile: "minimal",
    adapter_name: "remote:minimal",
    case_files: MINIMAL_CASES,
  });
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  process.exitCode = await main();
}
