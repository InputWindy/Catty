import fs from "node:fs/promises";
import path from "node:path";

const SENSITIVE_KEY_PATTERN =
  /authorization|api[_-]?key|secret|password|credential/i;

function sanitize(value) {
  if (Array.isArray(value)) {
    return value.map(sanitize);
  }
  if (value && typeof value === "object") {
    const output = {};
    for (const [key, entry] of Object.entries(value)) {
      if (SENSITIVE_KEY_PATTERN.test(key)) {
        output[key] = "[redacted]";
      } else {
        output[key] = sanitize(entry);
      }
    }
    return output;
  }
  return value;
}

export class AuditLog {
  constructor({ data_dir, file_name = "audit.jsonl" }) {
    this.data_dir = data_dir;
    this.file_path = path.join(data_dir, file_name);
    this.ready = fs.mkdir(data_dir, { recursive: true });
  }

  async write(record) {
    await this.ready;
    const safe_record = sanitize({
      timestamp_ms: Date.now(),
      request_id: record.request_id ?? null,
      session_id: record.session_id ?? null,
      provider: record.provider ?? "direct",
      user_message: record.user_message ?? null,
      before_revision: record.before_revision ?? null,
      after_revision: record.after_revision ?? null,
      tool_calls: record.tool_calls ?? [],
      tool_results: record.tool_results ?? [],
      error: record.error ?? null,
      duration_ms: record.duration_ms ?? 0,
      failed_tool_call_index: record.failed_tool_call_index ?? null,
    });
    await fs.appendFile(this.file_path, JSON.stringify(safe_record) + "\n", "utf8");
  }
}

export class NullAuditLog {
  async write() {}
}
