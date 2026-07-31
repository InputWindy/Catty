import { randomUUID } from "node:crypto";
import { pathToFileURL } from "node:url";
import { createDefaultToolRegistry } from "../src/tools/definitions.mjs";
import { RemoteWorldAdapter } from "../src/world/remote-world-adapter.mjs";
import { startFakeMahoWorldServer } from "../tests/helpers/fake-maho-world-server.mjs";

function toolCall(tool_name, args) {
  return {
    tool_call_id: randomUUID(),
    tool_name,
    args,
  };
}

export async function main({
  output = process.stdout,
  error_output = process.stderr,
} = {}) {
  const started_at = performance.now();
  let fake;
  let adapter;
  try {
    fake = await startFakeMahoWorldServer();
    const session_id = randomUUID();
    const world_id = randomUUID();
    adapter = new RemoteWorldAdapter({
      session_id,
      world_id,
      tool_registry: createDefaultToolRegistry(),
      base_url: fake.base_url,
      timeout_ms: 1_000,
    });
    const health = await adapter.health();
    const initial = await adapter.getSnapshot({
      request_id: randomUUID(),
      session_id,
      world_id,
    });
    const spawned = await adapter.executeTransaction({
      request_id: randomUUID(),
      session_id,
      world_id,
      expected_revision: initial.revision,
      dry_run: false,
      atomic: true,
      tool_calls: [
        toolCall("entity.spawn_primitive", {
          primitive_type: "cube",
        }),
      ],
    });
    const entity_id =
      spawned.tool_results[0]?.data?.entity?.entity_id;
    const transformed = await adapter.executeTransaction({
      request_id: randomUUID(),
      session_id,
      world_id,
      expected_revision: spawned.after_revision,
      dry_run: false,
      atomic: true,
      tool_calls: [
        toolCall("entity.set_transform", {
          entity_id,
          transform: { position: [3, 1, 5] },
        }),
      ],
    });
    const undone = await adapter.undo({
      request_id: randomUUID(),
      session_id,
      world_id,
      expected_revision: transformed.after_revision,
      undo_token: transformed.undo_token,
    });
    const final = await adapter.getSnapshot({
      request_id: randomUUID(),
      session_id,
      world_id,
    });

    if (
      !health.ok ||
      initial.revision !== 0 ||
      !spawned.ok ||
      !transformed.ok ||
      !undone.ok ||
      final.revision !== 3 ||
      final.entities.length !== 1 ||
      JSON.stringify(final.entities[0].transform.position) !==
        JSON.stringify([0, 0, 0])
    ) {
      throw new Error("Remote smoke assertions failed");
    }
    output.write(
      `Remote smoke PASS adapter=remote health=ok snapshot=ok spawn=ok transform=ok undo=ok final_revision=${final.revision} duration_ms=${Math.round(performance.now() - started_at)}\n`
    );
    return 0;
  } catch (error) {
    error_output.write(
      `Remote smoke FAIL: ${error?.message || String(error)}\n`
    );
    return 1;
  } finally {
    await adapter?.close();
    await fake?.close();
  }
}

if (import.meta.url === pathToFileURL(process.argv[1]).href) {
  process.exitCode = await main();
}
