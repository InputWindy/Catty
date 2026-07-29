export function sendJson(res, status_code, value) {
  const body = JSON.stringify(value);
  res.writeHead(status_code, {
    "Content-Type": "application/json; charset=utf-8",
    "Content-Length": Buffer.byteLength(body),
  });
  res.end(body);
}

export function sendAgentError(res, error, status_code) {
  return sendJson(res, status_code, {
    ok: false,
    error: error.toJSON(),
  });
}
