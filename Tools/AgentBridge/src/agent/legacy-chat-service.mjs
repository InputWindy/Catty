const SYSTEM_PREAMBLE =
  "You are the Catty engine in-editor Agent. Help the user write and refine Lua " +
  "game scripts for Catty (catty.log / cvars / packages / resources, Scripts/*.lua). " +
  "Do not rewrite engine C++ unless explicitly asked. Prefer complete, runnable Lua. " +
  "Keep answers concise unless the user asks for detail.\n\nUser message:\n";

export class LegacyChatService {
  constructor(config, { agent = null, owns_agent = false } = {}) {
    this.config = config;
    this.agent = agent;
    this.owns_agent = owns_agent;
    this.busy = false;
    this.status = config.force_mock
      ? "mock (no CURSOR_API_KEY)"
      : agent
        ? "starting"
        : `legacy mock (Agent Core provider: ${config.ai?.provider_id || "external"})`;
    this.next_id = 0;
    this.events = [];
  }

  async initialize() {
    console.log(
      `[CattyAgentBridge] provider=${
        this.config.ai?.provider_id || (this.config.force_mock ? "mock" : "cursor")
      } legacyMock=${this.isMock()}`
    );
    this.pushEvent(
      "system",
      this.config.force_mock
        ? "Agent bridge online in MOCK mode. Set CURSOR_API_KEY and restart the engine for a real Cursor Agent."
        : this.agent
          ? "Agent bridge starting Cursor local Agent..."
          : `Legacy chat remains in mock mode; Agent Core uses ${this.config.ai?.provider_id || "an external Provider"}.`
    );

    if (!this.agent) {
      return;
    }
    this.status = "ready";
    this.pushEvent("system", "Cursor local Agent ready. cwd=" + this.config.cwd);
  }

  isMock() {
    return this.config.force_mock || !this.agent;
  }

  getAgent() {
    return this.agent;
  }

  isBusy() {
    return this.busy;
  }

  getHealth() {
    return {
      ok: true,
      mock: this.isMock(),
      busy: this.busy,
      status: this.status,
      cwd: this.config.cwd,
    };
  }

  getEvents(after) {
    return {
      events: this.events.filter((event) => event.id > after),
      busy: this.busy,
      mock: this.isMock(),
      status: this.status,
    };
  }

  pushEvent(role, text) {
    const event = {
      id: this.next_id++,
      role,
      text: String(text ?? ""),
    };
    this.events.push(event);
    if (this.events.length > 500) {
      this.events.splice(0, this.events.length - 500);
    }
    return event;
  }

  async handleChat(message) {
    if (this.busy) {
      return { error: "Agent is busy" };
    }

    this.busy = true;
    const use_mock = this.isMock();
    this.status = use_mock ? "mock thinking..." : "thinking...";
    try {
      if (use_mock) {
        await new Promise((resolve) => setTimeout(resolve, 400));
        const reply =
          "(Mock Agent — set CURSOR_API_KEY for a real Cursor Agent)\n\n" +
          "You said:\n> " +
          message +
          "\n\nExample Lua:\n```lua\ncatty.log(\"hello from Agent\")\n```";
        this.pushEvent("assistant", reply);
        this.status = this.config.force_mock
          ? "mock (no CURSOR_API_KEY)"
          : "mock fallback";
        return { ok: true };
      }

      const run = await this.agent.send(SYSTEM_PREAMBLE + message);
      let text = "";
      for await (const event of run.stream()) {
        if (event.type === "assistant" && event.message?.content) {
          for (const block of event.message.content) {
            if (block.type === "text" && block.text) {
              text += block.text;
            }
          }
        }
      }
      await run.wait();
      if (!text.trim()) {
        text = "(Agent finished with empty text.)";
      }
      this.pushEvent("assistant", text);
      this.status = "ready";
      return { ok: true };
    } catch (error) {
      const message_text = error?.message || String(error);
      this.pushEvent("system", "Agent error: " + message_text);
      this.status = "error";
      return { error: message_text };
    } finally {
      this.busy = false;
    }
  }

  handleUnhandledError(error) {
    this.pushEvent("system", "Unhandled chat error: " + error);
    this.busy = false;
  }

  async close() {
    if (!this.owns_agent) {
      return;
    }
    try {
      if (this.agent && typeof this.agent[Symbol.asyncDispose] === "function") {
        await this.agent[Symbol.asyncDispose]();
      } else if (this.agent && typeof this.agent.close === "function") {
        await this.agent.close();
      }
    } catch {
      // Preserve the legacy shutdown behavior: disposal errors do not block exit.
    }
  }
}
