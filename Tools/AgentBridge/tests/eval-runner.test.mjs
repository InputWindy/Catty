import assert from "node:assert/strict";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { runEvaluations } from "../evals/eval-runner.mjs";
import { main as runEvalCommand } from "../evals/run-evals.mjs";

function outputSink() {
  return { write: () => true };
}

test("the checked-in behavior evaluation suite passes", async () => {
  const summary = await runEvaluations({ output: outputSink() });
  assert.equal(summary.total >= 26, true);
  assert.equal(summary.failed, 0);
  assert.equal(summary.passed, summary.total);
});

test("the evaluation command returns nonzero when any scenario fails", async (t) => {
  const temporary_dir = await fs.mkdtemp(
    path.join(os.tmpdir(), "catty-eval-test-")
  );
  t.after(() => fs.rm(temporary_dir, { recursive: true, force: true }));
  const case_file = path.join(temporary_dir, "intentional-failure.json");
  await fs.writeFile(
    case_file,
    JSON.stringify({
      cases: [
        {
          name: "intentional failure",
          turns: [
            {
              message: "生成一个立方体",
              expect: { revision: 999 },
            },
          ],
        },
      ],
    }),
    "utf8"
  );

  const exit_code = await runEvalCommand([case_file], {
    output: outputSink(),
    error_output: outputSink(),
  });
  assert.equal(exit_code, 1);
});
