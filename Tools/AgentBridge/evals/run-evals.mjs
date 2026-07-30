import { pathToFileURL } from "node:url";
import { runEvaluations } from "./eval-runner.mjs";

export async function main(
  argv = process.argv.slice(2),
  { output = process.stdout, error_output = process.stderr } = {}
) {
  const case_files = argv.length > 0 ? argv : undefined;
  try {
    const summary = await runEvaluations({ case_files, output });
    return summary.failed === 0 ? 0 : 1;
  } catch (error) {
    error_output.write(`Eval runner failed: ${error?.stack || error}\n`);
    return 1;
  }
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  process.exitCode = await main();
}
