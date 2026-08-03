import assert from "node:assert/strict";
import fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { randomUUID } from "node:crypto";
import test from "node:test";
import { AuditLog } from "../src/logging/audit-log.mjs";

test("JSONL audit records required fields without API keys or authorization", async (t) => {
  const data_dir = await fs.mkdtemp(path.join(os.tmpdir(), "maho-audit-test-"));
  t.after(() => fs.rm(data_dir, { recursive: true, force: true }));
  const audit_log = new AuditLog({ data_dir });
  const secret_value = "cursor-secret-value-for-test";
  const world_secret = "world-secret-value-for-test";

  await audit_log.write({
    request_id: randomUUID(),
    session_id: randomUUID(),
    provider: "mock",
    world_adapter: "remote",
    adapter_protocol_version: "1.0",
    adapter_request_phase: "execute",
    adapter_duration_ms: 7,
    adapter_http_status: 409,
    adapter_replayed: true,
    adapter_timeout: false,
    adapter_cancelled: false,
    remote_error_class: "revision_conflict",
    adapter_supported_tools: ["world.get_summary"],
    adapter_max_tool_calls: 1,
    adapter_supports_atomic: false,
    adapter_supports_dry_run: false,
    adapter_supports_undo: false,
    capability_rejection_reason: "capability_insufficient",
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
          world_auth_token: world_secret,
        },
      },
    ],
    tool_results: [{ ok: true }],
    error: null,
    duration_ms: 2,
  });

  const raw = await fs.readFile(path.join(data_dir, "audit.jsonl"), "utf8");
  assert.equal(raw.includes(secret_value), false);
  assert.equal(raw.includes(world_secret), false);
  const record = JSON.parse(raw.trim());
  assert.deepEqual(Object.keys(record), [
    "timestamp_ms",
    "request_id",
    "session_id",
    "provider",
    "model",
    "request_phase",
    "attempt_count",
    "http_status",
    "finish_reason",
    "tool_call_count",
    "input_tokens",
    "output_tokens",
    "total_tokens",
    "cached_input_tokens",
    "finalization_used",
    "finalization_failed",
    "timeout",
    "cancelled",
    "world_adapter",
    "adapter_protocol_version",
    "adapter_request_phase",
    "adapter_duration_ms",
    "adapter_http_status",
    "adapter_replayed",
    "adapter_timeout",
    "adapter_cancelled",
    "remote_error_class",
    "adapter_supported_tools",
    "adapter_max_tool_calls",
    "adapter_supports_atomic",
    "adapter_supports_dry_run",
    "adapter_supports_undo",
    "capability_rejection_reason",
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
  assert.equal(record.tool_calls[0].args.world_auth_token, "[redacted]");
  assert.equal(record.world_adapter, "remote");
  assert.equal(record.adapter_protocol_version, "1.0");
  assert.equal(record.adapter_request_phase, "execute");
  assert.equal(record.adapter_http_status, 409);
  assert.equal(record.adapter_replayed, true);
  assert.equal(record.remote_error_class, "revision_conflict");
  assert.deepEqual(record.adapter_supported_tools, ["world.get_summary"]);
  assert.equal(record.adapter_max_tool_calls, 1);
  assert.equal(record.adapter_supports_atomic, false);
  assert.equal(record.adapter_supports_dry_run, false);
  assert.equal(record.adapter_supports_undo, false);
  assert.equal(
    record.capability_rejection_reason,
    "capability_insufficient"
  );
});
