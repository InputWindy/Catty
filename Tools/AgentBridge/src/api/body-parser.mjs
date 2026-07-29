export class RequestBodyTooLargeError extends Error {
  constructor(limit_bytes) {
    super(`Request body exceeds ${limit_bytes} bytes`);
    this.name = "RequestBodyTooLargeError";
    this.limit_bytes = limit_bytes;
  }
}

export function readJson(req, { limit_bytes = 1024 * 1024 } = {}) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    let received_bytes = 0;
    let settled = false;

    req.on("data", (chunk) => {
      if (settled) {
        return;
      }
      received_bytes += chunk.length;
      if (received_bytes > limit_bytes) {
        settled = true;
        reject(new RequestBodyTooLargeError(limit_bytes));
        return;
      }
      chunks.push(chunk);
    });

    req.on("end", () => {
      if (settled) {
        return;
      }
      try {
        const raw = Buffer.concat(chunks).toString("utf8");
        resolve(raw ? JSON.parse(raw) : {});
      } catch (error) {
        reject(error);
      }
    });
    req.on("error", reject);
  });
}

