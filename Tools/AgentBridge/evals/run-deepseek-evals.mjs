import { pathToFileURL } from "node:url";
import { DEEPSEEK_EVAL_CASES } from "./deepseek-cases.mjs";
import {
  createRealProviderCore,
  hasDeepSeekKey,
} from "./real-provider-core.mjs";

export async function main({
  env = process.env,
  output = process.stdout,
  error_output = process.stderr,
} = {}) {
  if (!hasDeepSeekKey(env)) {
    error_output.write(
      "DeepSeek eval requires MAHO_AI_API_KEY or DEEPSEEK_API_KEY. No network request was made.\n"
    );
    return 1;
  }
  const started_at = performance.now();
  let core;
  let passed = 0;
  let failed = 0;
  try {
    core = await createRealProviderCore({ env });
    const metadata = core.provider.getMetadata();
    output.write(
      `DeepSeek eval provider=${metadata.provider} model=${metadata.model} cases=${DEEPSEEK_EVAL_CASES.length}\n`
    );
    for (const scenario of DEEPSEEK_EVAL_CASES) {
      const session = core.createSession(scenario.initial_entities);
      let case_ok = true;
      for (const turn of scenario.turns) {
        const { result, plan_tool_names } = await core.run(
          session,
          turn.message
        );
        const expected_names = turn.acceptable_tool_names ||
          (turn.tool_name === null ? [] : [turn.tool_name]);
        const tool_names_match = turn.acceptable_tool_names
          ? plan_tool_names.length === 1 &&
            expected_names.includes(plan_tool_names[0])
          : JSON.stringify(plan_tool_names) ===
            JSON.stringify(expected_names);
        if (
          !result.ok ||
          !tool_names_match
        ) {
          case_ok = false;
          break;
        }
      }
      case_ok =
        case_ok &&
        scenario.verify(await core.getSnapshot(session));
      if (case_ok) {
        passed += 1;
        output.write(`PASS ${scenario.name}\n`);
      } else {
        failed += 1;
        output.write(`FAIL ${scenario.name}\n`);
      }
    }
    const duration_ms = Math.round(performance.now() - started_at);
    const usage = core.audit_log.usage();
    output.write(
      `DeepSeek eval summary: provider=${metadata.provider} model=${metadata.model} passed=${passed} failed=${failed} duration_ms=${duration_ms} usage=${JSON.stringify(usage)}\n`
    );
    return failed === 0 ? 0 : 1;
  } catch (error) {
    error_output.write(`DeepSeek eval failed: ${error?.message || String(error)}\n`);
    return 1;
  } finally {
    await core?.close();
  }
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  process.exitCode = await main();
}
