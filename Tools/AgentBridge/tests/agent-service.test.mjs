import assert from "node:assert/strict";
import { randomUUID } from "node:crypto";
import test from "node:test";
import { AgentService } from "../src/agent/agent-service.mjs";
import { MockProvider } from "../src/agent/providers/mock-provider.mjs";
import { createTestCore } from "./helpers/core.mjs";

function createService(core, provider = new MockProvider()) {
  return new AgentService({
    session_manager: core.session_manager,
    tool_registry: core.tool_registry,
    command_executor: core.command_executor,
    provider,
    audit_log: { write: async () => {} },
  });
}

function request(core, message, overrides = {}) {
  return {
    protocol_version: "1.0",
    request_id: randomUUID(),
    session_id: core.session.session_id,
    message,
    expected_revision: core.world.revision,
    ...overrides,
  };
}

test("MockProvider turns a red cube request into authoritative execution", async () => {
  const core = createTestCore();
  const service = createService(core);
  const result = await service.run(request(core, "生成一个红色立方体"));

  assert.equal(result.ok, true);
  assert.equal(result.world_revision, 1);
  assert.equal(result.tool_results[0].ok, true);
  assert.deepEqual(
    result.tool_results[0].data.entity.properties.color,
    [1, 0, 0, 1]
  );
});

test("MockProvider lists, queries, deletes, and undoes entities", async () => {
  const core = createTestCore();
  const service = createService(core);
  const spawned = await service.run(request(core, "生成一个立方体"));
  const entity = spawned.tool_results[0].data.entity;

  const listed = await service.run(request(core, "列出所有实体"));
  assert.equal(listed.tool_results[0].data.entities.length, 1);

  const queried = await service.run(request(core, `查询 ${entity.name}`));
  assert.equal(queried.tool_results[0].data.entity.entity_id, entity.entity_id);

  const deleted = await service.run(request(core, `删除 ${entity.entity_id}`));
  assert.equal(deleted.ok, true);
  assert.equal(core.world.entities.size, 0);

  const undone = await service.run(request(core, "撤销刚才的操作"));
  assert.equal(undone.ok, true);
  assert.equal(core.world.entities.size, 1);
});

test("invalid provider output returns MODEL_OUTPUT_INVALID", async () => {
  const core = createTestCore();
  const service = createService(core, {
    name: "invalid",
    run: async () => ({
      assistant_message: "invalid",
      tool_calls: "not-an-array",
    }),
  });
  const result = await service.run(request(core, "anything"));

  assert.equal(result.ok, false);
  assert.equal(result.error.code, "MODEL_OUTPUT_INVALID");
});

test("provider ToolCalls missing required fields are MODEL_OUTPUT_INVALID", async () => {
  const core = createTestCore();
  const service = createService(core, {
    name: "invalid-tool-call",
    run: async () => ({
      assistant_message: "invalid",
      tool_calls: [{ tool_name: "world.get_summary", args: {} }],
    }),
  });
  const result = await service.run(request(core, "anything"));

  assert.equal(result.ok, false);
  assert.equal(result.error.code, "MODEL_OUTPUT_INVALID");
});

test("assistant message cannot claim success when a command fails", async () => {
  const core = createTestCore();
  const service = createService(core, {
    name: "failure-test",
    run: async ({ world_snapshot }) => ({
      assistant_message: "已经成功完成。",
      tool_calls: [
        {
          tool_call_id: randomUUID(),
          tool_name: "entity.destroy",
          expected_revision: world_snapshot.revision,
          dry_run: false,
          args: { entity_id: randomUUID() },
        },
      ],
    }),
  });
  const result = await service.run(request(core, "delete"));

  assert.equal(result.ok, false);
  assert.equal(result.error.code, "ENTITY_NOT_FOUND");
  assert.doesNotMatch(result.assistant_message, /成功完成/);
});

test("AgentService request_id replay bypasses a now-stale expected revision", async () => {
  const core = createTestCore();
  const service = createService(core);
  const request_id = randomUUID();
  const first = await service.run(
    request(core, "生成一个立方体", { request_id })
  );
  const replay = await service.run(
    request(core, "不同消息", {
      request_id,
      expected_revision: 0,
    })
  );

  assert.equal(first.ok, true);
  assert.equal(replay.ok, true);
  assert.equal(replay.replayed, true);
  assert.equal(core.world.entities.size, 1);
  assert.equal(core.world.revision, 1);
});
