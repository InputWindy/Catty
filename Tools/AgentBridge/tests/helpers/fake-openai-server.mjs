import http from "node:http";

async function readRequestBody(req) {
  const chunks = [];
  for await (const chunk of req) {
    chunks.push(chunk);
  }
  const raw = Buffer.concat(chunks).toString("utf8");
  return raw ? JSON.parse(raw) : {};
}

export async function startFakeOpenAIServer(handler) {
  const requests = [];
  const server = http.createServer(async (req, res) => {
    try {
      const body = await readRequestBody(req);
      const request = {
        method: req.method,
        url: req.url,
        headers: { ...req.headers },
        body,
      };
      requests.push(request);
      const result = await handler(request, requests.length);
      if (result?.delay_ms) {
        await new Promise((resolve) => setTimeout(resolve, result.delay_ms));
      }
      if (result?.destroy) {
        res.destroy();
        return;
      }
      const status = result?.status ?? 200;
      const headers = result?.headers || {};
      const payload =
        result?.raw ??
        JSON.stringify(
          result?.body ?? {
            choices: [
              {
                message: { content: "ok" },
                finish_reason: "stop",
              },
            ],
          }
        );
      res.writeHead(status, {
        "content-type": "application/json",
        ...headers,
      });
      res.end(payload);
    } catch (error) {
      if (!res.headersSent) {
        res.writeHead(500, { "content-type": "application/json" });
      }
      res.end(JSON.stringify({ error: error.message }));
    }
  });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const address = server.address();
  return {
    base_url: `http://127.0.0.1:${address.port}/v1/`,
    requests,
    async close() {
      server.closeAllConnections();
      if (server.listening) {
        await new Promise((resolve) => server.close(resolve));
      }
    },
  };
}

export function completion({
  content = "",
  tool_calls,
  finish_reason = "stop",
  usage,
} = {}) {
  return {
    choices: [
      {
        message: {
          content,
          ...(tool_calls === undefined ? {} : { tool_calls }),
        },
        finish_reason,
      },
    ],
    ...(usage === undefined ? {} : { usage }),
  };
}

export function providerToolCall({
  id,
  name,
  arguments: arguments_json = "{}",
}) {
  return {
    ...(id === undefined ? {} : { id }),
    type: "function",
    function: {
      name,
      arguments: arguments_json,
    },
  };
}
