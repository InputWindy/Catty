import { randomUUID } from "node:crypto";
import { UUID_PATTERN } from "../../protocol/schemas.mjs";
import {
  DEFAULT_PROVIDER_CAPABILITIES,
  assertProviderPlanInput,
  createProviderOutput,
} from "../provider-contract.mjs";
import {
  assertSafePlainObject,
  isPlainObject,
} from "../normalized-messages.mjs";
import {
  ToolNameMapper,
  normalizedToolCallsToChatCompletions,
  toolDefinitionsToChatCompletions,
} from "../provider-tools.mjs";
import {
  ProviderError,
  asProviderError,
  providerErrorReasons,
} from "./provider-errors.mjs";

const UUID_REGEX = new RegExp(UUID_PATTERN);
const RETRYABLE_HTTP_STATUS = new Set([408, 429]);
const DEFAULT_RETRY_AFTER_LIMIT_MS = 5_000;

function normalizeBaseUrl(base_url) {
  let parsed;
  try {
    parsed = new URL(base_url);
  } catch {
    throw new ProviderError(
      providerErrorReasons.CONFIGURATION_MISSING,
      "CATTY_AI_BASE_URL must be a valid HTTP or HTTPS URL",
      { details: { field: "CATTY_AI_BASE_URL" } }
    );
  }
  if (
    !["http:", "https:"].includes(parsed.protocol) ||
    parsed.username ||
    parsed.password ||
    parsed.search ||
    parsed.hash
  ) {
    throw new ProviderError(
      providerErrorReasons.CONFIGURATION_MISSING,
      "CATTY_AI_BASE_URL must be a credential-free HTTP or HTTPS URL",
      { details: { field: "CATTY_AI_BASE_URL" } }
    );
  }
  parsed.pathname = parsed.pathname.replace(/\/+$/, "");
  return parsed.toString().replace(/\/$/, "");
}

function endpointFromBaseUrl(base_url) {
  return `${normalizeBaseUrl(base_url)}/chat/completions`;
}

function isRetryableStatus(status) {
  return RETRYABLE_HTTP_STATUS.has(status) || status >= 500;
}

function parseRetryAfterMs(value, limit_ms) {
  if (!value) {
    return 0;
  }
  const seconds = Number(value);
  if (Number.isFinite(seconds) && seconds >= 0) {
    return Math.min(Math.round(seconds * 1_000), limit_ms);
  }
  const date_ms = Date.parse(value);
  if (!Number.isFinite(date_ms)) {
    return 0;
  }
  return Math.min(Math.max(0, date_ms - Date.now()), limit_ms);
}

function abortableSleep(delay_ms, signal) {
  if (delay_ms <= 0) {
    return Promise.resolve();
  }
  return new Promise((resolve, reject) => {
    const cleanup = () => signal?.removeEventListener("abort", onAbort);
    const timer = setTimeout(() => {
      cleanup();
      resolve();
    }, delay_ms);
    const onAbort = () => {
      clearTimeout(timer);
      cleanup();
      reject(signal.reason || new Error("Request cancelled"));
    };
    if (signal?.aborted) {
      onAbort();
      return;
    }
    signal?.addEventListener("abort", onAbort, { once: true });
  });
}

function normalizeOpenAIUsage(usage) {
  if (!isPlainObject(usage)) {
    return {
      input_tokens: null,
      output_tokens: null,
      total_tokens: null,
      cached_input_tokens: null,
    };
  }
  const cached = usage.prompt_tokens_details?.cached_tokens;
  return {
    input_tokens: usage.prompt_tokens ?? null,
    output_tokens: usage.completion_tokens ?? null,
    total_tokens: usage.total_tokens ?? null,
    cached_input_tokens: cached ?? null,
  };
}

function messagesToChatCompletions(messages, mapper) {
  return messages.map((message) => {
    if (message.role === "assistant") {
      const converted = {
        role: "assistant",
        content: message.content,
      };
      if (message.tool_calls.length > 0) {
        converted.tool_calls = normalizedToolCallsToChatCompletions(
          message.tool_calls,
          mapper
        );
      }
      return converted;
    }
    if (message.role === "tool") {
      return {
        role: "tool",
        tool_call_id: message.tool_call_id,
        name: mapper.toProviderName(message.name),
        content: message.content,
      };
    }
    return {
      role: message.role,
      content: message.content,
    };
  });
}

function parseArguments(raw_arguments, index) {
  if (typeof raw_arguments !== "string") {
    throw new ProviderError(
      providerErrorReasons.TOOL_ARGUMENTS_INVALID,
      "Model ToolCall arguments must be a JSON string",
      { details: { tool_call_index: index } }
    );
  }
  let args;
  try {
    args = JSON.parse(raw_arguments);
  } catch {
    throw new ProviderError(
      providerErrorReasons.TOOL_ARGUMENTS_INVALID,
      "Model ToolCall arguments are not valid JSON",
      { details: { tool_call_index: index } }
    );
  }
  try {
    return assertSafePlainObject(args, `tool_calls[${index}].args`);
  } catch {
    throw new ProviderError(
      providerErrorReasons.TOOL_ARGUMENTS_INVALID,
      "Model ToolCall arguments must be a safe plain object",
      { details: { tool_call_index: index } }
    );
  }
}

function parseChatCompletion(
  payload,
  {
    provider,
    model,
    mapper,
    phase,
    max_tool_calls,
    attempt_count,
    duration_ms,
    http_status,
  }
) {
  if (
    !isPlainObject(payload) ||
    !Array.isArray(payload.choices) ||
    payload.choices.length === 0 ||
    !isPlainObject(payload.choices[0]) ||
    !isPlainObject(payload.choices[0].message)
  ) {
    throw new ProviderError(
      providerErrorReasons.RESPONSE_INVALID,
      "Provider response is missing choices[0].message",
      { provider, model, phase, http_status, attempt_count }
    );
  }
  const choice = payload.choices[0];
  const message = choice.message;
  const assistant_message =
    message.content === null || message.content === undefined
      ? ""
      : message.content;
  if (typeof assistant_message !== "string") {
    throw new ProviderError(
      providerErrorReasons.RESPONSE_INVALID,
      "Provider response message content must be text or null",
      { provider, model, phase, http_status, attempt_count }
    );
  }
  const raw_tool_calls = message.tool_calls ?? [];
  if (!Array.isArray(raw_tool_calls)) {
    throw new ProviderError(
      providerErrorReasons.RESPONSE_INVALID,
      "Provider response tool_calls must be an array",
      { provider, model, phase, http_status, attempt_count }
    );
  }
  if (raw_tool_calls.length > max_tool_calls) {
    throw new ProviderError(
      providerErrorReasons.TOOL_CALL_LIMIT_EXCEEDED,
      "Provider returned more ToolCalls than the configured limit",
      {
        provider,
        model,
        phase,
        http_status,
        attempt_count,
        details: {
          tool_call_count: raw_tool_calls.length,
          max_tool_calls,
        },
      }
    );
  }
  if (phase === "finalize" && raw_tool_calls.length > 0) {
    throw new ProviderError(
      providerErrorReasons.FINALIZATION_TOOL_CALL,
      "Provider finalization returned a ToolCall",
      {
        provider,
        model,
        phase,
        http_status,
        attempt_count,
        details: { tool_call_count: raw_tool_calls.length },
      }
    );
  }
  const tool_calls = raw_tool_calls.map((raw_tool_call, index) => {
    if (
      !isPlainObject(raw_tool_call) ||
      !isPlainObject(raw_tool_call.function) ||
      typeof raw_tool_call.function.name !== "string" ||
      !raw_tool_call.function.name
    ) {
      throw new ProviderError(
        providerErrorReasons.RESPONSE_INVALID,
        "Provider returned an incomplete ToolCall",
        {
          provider,
          model,
          phase,
          http_status,
          attempt_count,
          details: { tool_call_index: index },
        }
      );
    }
    return {
      tool_call_id:
        typeof raw_tool_call.id === "string" &&
        UUID_REGEX.test(raw_tool_call.id)
          ? raw_tool_call.id
          : randomUUID(),
      tool_name: mapper.toInternalName(raw_tool_call.function.name),
      args: parseArguments(raw_tool_call.function.arguments, index),
    };
  });
  return createProviderOutput({
    provider,
    model,
    assistant_message,
    tool_calls,
    finish_reason:
      choice.finish_reason === null ||
      typeof choice.finish_reason === "string"
        ? choice.finish_reason
        : null,
    usage: normalizeOpenAIUsage(payload.usage),
    provider_metadata: {
      phase,
      attempt_count,
      duration_ms,
      http_status,
      timeout: false,
      cancelled: false,
    },
  });
}

export class OpenAICompatibleProvider {
  constructor({
    provider_id = "openai-compatible",
    base_url,
    model,
    api_key,
    timeout_ms = 30_000,
    max_retries = 1,
    temperature = 0,
    finalize = true,
    max_tool_calls = 16,
    request_extras = {},
    fetch_impl = globalThis.fetch,
    sleep_impl = abortableSleep,
    retry_after_limit_ms = DEFAULT_RETRY_AFTER_LIMIT_MS,
  }) {
    if (!api_key) {
      throw new ProviderError(
        providerErrorReasons.API_KEY_MISSING,
        `An API key is required for Provider ${provider_id}`,
        { provider: provider_id, model }
      );
    }
    if (!model) {
      throw new ProviderError(
        providerErrorReasons.CONFIGURATION_MISSING,
        `A model is required for Provider ${provider_id}`,
        { provider: provider_id, details: { field: "CATTY_AI_MODEL" } }
      );
    }
    if (typeof fetch_impl !== "function") {
      throw new TypeError("OpenAICompatibleProvider requires fetch");
    }
    assertSafePlainObject(request_extras, "request_extras");
    this.name = provider_id;
    this.model = model;
    this.base_url = normalizeBaseUrl(base_url);
    this.endpoint = endpointFromBaseUrl(base_url);
    this.api_key = api_key;
    this.timeout_ms = timeout_ms;
    this.max_retries = max_retries;
    this.temperature = temperature;
    this.finalization_enabled = finalize;
    this.max_tool_calls = max_tool_calls;
    this.request_extras = structuredClone(request_extras);
    this.fetch_impl = fetch_impl;
    this.sleep_impl = sleep_impl;
    this.retry_after_limit_ms = retry_after_limit_ms;
    this.active_requests = new Map();
    this.closed = false;
    this.capabilities = Object.freeze({
      ...DEFAULT_PROVIDER_CAPABILITIES,
      supports_finalization: true,
    });
  }

  getMetadata() {
    return {
      provider: this.name,
      model: this.model,
      ready: !this.closed,
      real: true,
      thinking: "disabled",
      endpoint_origin: new URL(this.base_url).origin,
      capabilities: { ...this.capabilities },
    };
  }

  async plan(input) {
    assertProviderPlanInput(input);
    const mapper = new ToolNameMapper(input.tool_definitions);
    return this.#request({
      input,
      mapper,
      phase: "plan",
      tools: toolDefinitionsToChatCompletions(
        input.tool_definitions,
        mapper
      ),
    });
  }

  async finalize(input) {
    assertProviderPlanInput(input);
    const mapper = new ToolNameMapper(input.tool_definitions);
    return this.#request({
      input,
      mapper,
      phase: "finalize",
      tools: null,
    });
  }

  async #request({ input, mapper, phase, tools }) {
    if (this.closed) {
      throw new ProviderError(
        providerErrorReasons.REQUEST_CANCELLED,
        "Provider is closed",
        {
          provider: this.name,
          model: this.model,
          phase,
          cancelled: true,
        }
      );
    }
    if (input.signal?.aborted) {
      throw new ProviderError(
        providerErrorReasons.REQUEST_CANCELLED,
        "Provider request was cancelled",
        {
          provider: this.name,
          model: this.model,
          phase,
          cancelled: true,
        }
      );
    }
    const body = {
      model: this.model,
      messages: messagesToChatCompletions(
        input.normalized_messages,
        mapper
      ),
      temperature: this.temperature,
      stream: false,
      ...structuredClone(this.request_extras),
    };
    if (tools) {
      body.tools = tools;
      body.tool_choice = "auto";
    }

    const started_at = performance.now();
    const max_attempts = this.max_retries + 1;
    let last_error;
    for (let attempt = 1; attempt <= max_attempts; attempt += 1) {
      try {
        const { payload, status } = await this.#fetchAttempt({
          body,
          signal: input.signal,
          phase,
          attempt,
        });
        try {
          return parseChatCompletion(payload, {
            provider: this.name,
            model: this.model,
            mapper,
            phase,
            max_tool_calls: this.max_tool_calls,
            attempt_count: attempt,
            duration_ms: Math.round(performance.now() - started_at),
            http_status: status,
          });
        } catch (parse_error) {
          if (parse_error instanceof ProviderError) {
            parse_error.provider ||= this.name;
            parse_error.model ||= this.model;
            parse_error.phase ||= phase;
            parse_error.http_status ??= status;
            throw parse_error;
          }
          throw new ProviderError(
            providerErrorReasons.RESPONSE_INVALID,
            "Provider response failed contract validation",
            {
              provider: this.name,
              model: this.model,
              phase,
              http_status: status,
              attempt_count: attempt,
              cause: parse_error,
            }
          );
        }
      } catch (error) {
        const provider_error = asProviderError(error, {
          reason: providerErrorReasons.CONNECTION_FAILED,
          message: "Provider connection failed",
          provider: this.name,
          model: this.model,
          phase,
        });
        provider_error.provider ||= this.name;
        provider_error.model ||= this.model;
        provider_error.phase ||= phase;
        provider_error.attempt_count = attempt;
        last_error = provider_error;
        if (!provider_error.retryable || attempt >= max_attempts) {
          throw provider_error;
        }
        const retry_after_ms =
          provider_error.details.retry_after_ms ?? 0;
        try {
          await this.sleep_impl(retry_after_ms, input.signal);
        } catch (sleep_error) {
          throw new ProviderError(
            providerErrorReasons.REQUEST_CANCELLED,
            "Provider request was cancelled during retry delay",
            {
              provider: this.name,
              model: this.model,
              phase,
              attempt_count: attempt,
              cancelled: true,
              cause: sleep_error,
            }
          );
        }
      }
    }
    throw last_error;
  }

  async #fetchAttempt({ body, signal, phase, attempt }) {
    const controller = new AbortController();
    const state = { timeout: false, shutdown: false };
    this.active_requests.set(controller, state);
    const onExternalAbort = () => controller.abort(signal.reason);
    signal?.addEventListener("abort", onExternalAbort, { once: true });
    const timer = setTimeout(() => {
      state.timeout = true;
      controller.abort(new Error("Provider request timed out"));
    }, this.timeout_ms);
    let response;
    try {
      response = await this.fetch_impl(this.endpoint, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          Authorization: `Bearer ${this.api_key}`,
        },
        body: JSON.stringify(body),
        signal: controller.signal,
      });
      if (!response.ok) {
        const retryable = isRetryableStatus(response.status);
        throw new ProviderError(
          providerErrorReasons.HTTP_ERROR,
          `Provider request failed with HTTP ${response.status}`,
          {
            provider: this.name,
            model: this.model,
            phase,
            http_status: response.status,
            attempt_count: attempt,
            retryable,
            details: {
              http_status: response.status,
              retry_after_ms: parseRetryAfterMs(
                response.headers.get("retry-after"),
                this.retry_after_limit_ms
              ),
            },
          }
        );
      }
      let text;
      try {
        text = await response.text();
      } catch (error) {
        throw new ProviderError(
          providerErrorReasons.CONNECTION_FAILED,
          "Provider connection ended before the response completed",
          {
            provider: this.name,
            model: this.model,
            phase,
            http_status: response.status,
            attempt_count: attempt,
            retryable: true,
            cause: error,
          }
        );
      }
      try {
        return {
          payload: JSON.parse(text),
          status: response.status,
        };
      } catch {
        throw new ProviderError(
          providerErrorReasons.RESPONSE_INVALID_JSON,
          "Provider response was not valid JSON",
          {
            provider: this.name,
            model: this.model,
            phase,
            http_status: response.status,
            attempt_count: attempt,
          }
        );
      }
    } catch (error) {
      if (error instanceof ProviderError) {
        throw error;
      }
      if (state.timeout) {
        throw new ProviderError(
          providerErrorReasons.REQUEST_TIMEOUT,
          `Provider request timed out after ${this.timeout_ms} ms`,
          {
            provider: this.name,
            model: this.model,
            phase,
            attempt_count: attempt,
            retryable: false,
            timeout: true,
            cause: error,
          }
        );
      }
      if (state.shutdown || signal?.aborted || controller.signal.aborted) {
        throw new ProviderError(
          providerErrorReasons.REQUEST_CANCELLED,
          "Provider request was cancelled",
          {
            provider: this.name,
            model: this.model,
            phase,
            attempt_count: attempt,
            retryable: false,
            cancelled: true,
            cause: error,
          }
        );
      }
      throw new ProviderError(
        providerErrorReasons.CONNECTION_FAILED,
        "Provider connection failed",
        {
          provider: this.name,
          model: this.model,
          phase,
          attempt_count: attempt,
          retryable: true,
          cause: error,
        }
      );
    } finally {
      clearTimeout(timer);
      signal?.removeEventListener("abort", onExternalAbort);
      this.active_requests.delete(controller);
    }
  }

  async close() {
    this.closed = true;
    for (const [controller, state] of this.active_requests) {
      state.shutdown = true;
      controller.abort(new Error("Provider shutdown"));
    }
  }
}
