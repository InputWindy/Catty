import { randomUUID } from "node:crypto";

const UUID_IN_TEXT =
  /[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-8][0-9a-fA-F]{3}-[89abAB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}/;

function makeToolCall(tool_name, args, revision) {
  return {
    tool_call_id: randomUUID(),
    tool_name,
    expected_revision: revision,
    dry_run: false,
    args,
  };
}

function findMentionedEntity(message, entities) {
  const uuid = message.match(UUID_IN_TEXT)?.[0];
  if (uuid) {
    return entities.find((entity) => entity.entity_id === uuid);
  }
  const by_name = entities.find(
    (entity) => entity.name && message.includes(entity.name)
  );
  if (by_name) {
    return by_name;
  }
  return entities.length === 1 ? entities[0] : undefined;
}

export class MockProvider {
  constructor() {
    this.name = "mock";
  }

  async run({ message, world_snapshot }) {
    const normalized = message.trim().toLowerCase();
    const revision = world_snapshot.revision;
    const entities = world_snapshot.entities;

    if (/撤销|undo/.test(normalized)) {
      return {
        assistant_message: "已请求撤销最近一次成功的世界修改。",
        tool_calls: [makeToolCall("history.undo", {}, revision)],
      };
    }

    if (/列出.*实体|所有实体|list.*entit/.test(normalized)) {
      return {
        assistant_message: "下面是当前实体列表。",
        tool_calls: [makeToolCall("world.query_entities", {}, revision)],
      };
    }

    if (/删除|销毁|destroy|delete/.test(normalized)) {
      const entity = findMentionedEntity(message, entities);
      if (!entity) {
        return {
          assistant_message: "请提供要删除的 entity_id 或明确的实体名称。",
          tool_calls: [],
        };
      }
      return {
        assistant_message: `已删除实体 ${entity.name}。`,
        tool_calls: [
          makeToolCall(
            "entity.destroy",
            { entity_id: entity.entity_id },
            revision
          ),
        ],
      };
    }

    if (/查询|查看|inspect|query|get/.test(normalized)) {
      const entity = findMentionedEntity(message, entities);
      if (!entity) {
        return {
          assistant_message: "请提供要查询的 entity_id 或明确的实体名称。",
          tool_calls: [],
        };
      }
      return {
        assistant_message: `下面是实体 ${entity.name} 的当前信息。`,
        tool_calls: [
          makeToolCall(
            "entity.get",
            { entity_id: entity.entity_id },
            revision
          ),
        ],
      };
    }

    if (/红色.*立方体|red.*cube/.test(normalized)) {
      return {
        assistant_message: "已生成一个红色立方体。",
        tool_calls: [
          makeToolCall(
            "entity.spawn_primitive",
            {
              primitive_type: "cube",
              properties: { color: [1, 0, 0, 1] },
            },
            revision
          ),
        ],
      };
    }

    if (/立方体|cube/.test(normalized)) {
      return {
        assistant_message: "已生成一个立方体。",
        tool_calls: [
          makeToolCall(
            "entity.spawn_primitive",
            { primitive_type: "cube" },
            revision
          ),
        ],
      };
    }

    return {
      assistant_message:
        "MockProvider 未识别该意图。可尝试生成立方体、列出实体、查询/删除实体或撤销。",
      tool_calls: [],
    };
  }
}

