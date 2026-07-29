import assert from "node:assert/strict";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { randomUUID } from "node:crypto";
import test from "node:test";
import { AuditLog } from "../src/logging/audit-log.mjs";

test("JSONL audit records required fields without API keys or authorization", async (t) => {
  const data_dir = await fs.mkdtemp(path.join(os.tmpdir(), "catty-audit-test-"));
  t.after(() => fs.rm(data_dir, { recursive: true, force: true }));
  const audit_log = new AuditLog({ data_dir });
  const secret_value = "cursor-secret-value-for-test";

  await audit_log.write({
    request_id: randomUUID(),
    session_id: randomUUID(),
    provider: "mock",
    user_message: "spawn a cube",
    before_revision: 0,
    after_revision: 1,
    tool_calls: [
      {
        tool_name: "entity.spawn_primitive",
        args: {
          primitive_type: "cube",
          cursor_api_key: secret_value,
          authorization: `Bearer ${secret_value}`,
        },
      },
    ],
    tool_results: [{ ok: true }],
    error: null,
    duration_ms: 2,
  });

  const raw = await fs.readFile(path.join(data_dir, "audit.jsonl"), "utf8");
  assert.equal(raw.includes(secret_value), false);
  const record = JSON.parse(raw.trim());
  assert.deepEqual(Object.keys(record), [
    "timestamp_ms",
    "request_id",
    "session_id",
    "provider",
    "user_message",
    "before_revision",
    "after_revision",
    "tool_calls",
    "tool_results",
    "error",
    "duration_ms",
    "failed_tool_call_index",
  ]);
  assert.equal(
    record.tool_calls[0].args.cursor_api_key,
    "[redacted]"
  );
  assert.equal(record.tool_calls[0].args.authorization, "[redacted]");
});

