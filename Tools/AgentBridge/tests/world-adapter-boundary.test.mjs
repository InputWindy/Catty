import assert from "node:assert/strict";
import { readdir, readFile } from "node:fs/promises";
import path from "node:path";
import test from "node:test";

async function modulesBelow(directory) {
  const entries = await readdir(directory, { withFileTypes: true });
  const nested = await Promise.all(
    entries.map(async (entry) => {
      const absolute = path.join(directory, entry.name);
      if (entry.isDirectory()) {
        return modulesBelow(absolute);
      }
      return entry.name.endsWith(".mjs") ? [absolute] : [];
    })
  );
  return nested.flat();
}

test("Agent Core and provider modules do not import MockWorld or UndoJournal", async () => {
  const roots = [
    "src/agent",
    "src/api",
    "src/cli",
    "src/execution",
    "src/sessions",
    "evals",
  ];
  const files = (
    await Promise.all(roots.map((root) => modulesBelow(root)))
  ).flat();
  files.push("server.mjs");

  for (const file of files) {
    const source = await readFile(file, "utf8");
    assert.doesNotMatch(
      source,
      /(?:from|import\()\s*["'][^"']*(?:mock-world|undo-journal)\.mjs["']/,
      file
    );
  }

  const remote_source = await readFile(
    "src/world/remote-world-adapter.mjs",
    "utf8"
  );
  assert.doesNotMatch(
    remote_source,
    /(?:mock-world|undo-journal|mock-world-commands)\.mjs/
  );

  const provider_files = await modulesBelow("src/agent/providers");
  for (const file of provider_files) {
    const source = await readFile(file, "utf8");
    assert.doesNotMatch(
      source,
      /world\/(?:mock-world-adapter|remote-world-adapter|remote-world-client|world-adapter-factory)\.mjs/,
      file
    );
  }
});
