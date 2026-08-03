import fs from "node:fs/promises";
import path from "node:path";

const SENSITIVE_KEY_PATTERN =
  /authorization|auth[_-]?token|api[_-]?key|secret|password|credential|reasoning_content/i;

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
      model: record.model ?? null,
      request_phase: record.request_phase ?? "execute",
      attempt_count: record.attempt_count ?? 0,
      http_status: record.http_status ?? null,
      finish_reason: record.finish_reason ?? null,
      tool_call_count:
        record.tool_call_count ?? record.tool_calls?.length ?? 0,
      input_tokens: record.input_tokens ?? null,
      output_tokens: record.output_tokens ?? null,
      total_tokens: record.total_tokens ?? null,
      cached_input_tokens: record.cached_input_tokens ?? null,
      finalization_used: record.finalization_used ?? false,
      finalization_failed: record.finalization_failed ?? false,
      timeout: record.timeout ?? false,
      cancelled: record.cancelled ?? false,
      world_adapter: record.world_adapter ?? null,
      adapter_protocol_version:
        record.adapter_protocol_version ?? null,
      adapter_request_phase:
        record.adapter_request_phase ?? null,
      adapter_duration_ms:
        record.adapter_duration_ms ?? 0,
      adapter_http_status:
        record.adapter_http_status ?? null,
      adapter_replayed:
        record.adapter_replayed ?? false,
      adapter_timeout:
        record.adapter_timeout ?? false,
      adapter_cancelled:
        record.adapter_cancelled ?? false,
      remote_error_class:
        record.remote_error_class ?? null,
      adapter_supported_tools:
        record.adapter_supported_tools ?? [],
      adapter_max_tool_calls:
        record.adapter_max_tool_calls ?? null,
      adapter_supports_atomic:
        record.adapter_supports_atomic ?? null,
      adapter_supports_dry_run:
        record.adapter_supports_dry_run ?? null,
      adapter_supports_undo:
        record.adapter_supports_undo ?? null,
      capability_rejection_reason:
        record.capability_rejection_reason ?? null,
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
