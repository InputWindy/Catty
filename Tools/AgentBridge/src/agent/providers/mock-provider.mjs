import { randomUUID } from "node:crypto";
import {
  POSITION_LIMIT,
  SCALE_MAX,
  SCALE_MIN,
} from "../../protocol/schemas.mjs";
import {
  DEFAULT_PROVIDER_CAPABILITIES,
  createProviderOutput,
} from "../provider-contract.mjs";

const UUID_IN_TEXT =
  /[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-8][0-9a-fA-F]{3}-[89abAB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}/;
const NUMBER_TEXT = String.raw`[-+]?(?:\d+(?:\.\d+)?|\.\d+)`;
const VECTOR_SEPARATOR = String.raw`\s*(?:,|，|\s)\s*`;

const COLORS = [
  { pattern: /红色|红|red/, name: "红色", value: [1, 0, 0, 1] },
  { pattern: /绿色|绿|green/, name: "绿色", value: [0, 1, 0, 1] },
  { pattern: /蓝色|蓝|blue/, name: "蓝色", value: [0, 0, 1, 1] },
  { pattern: /白色|白|white/, name: "白色", value: [1, 1, 1, 1] },
];

const PRIMITIVES = [
  {
    type: "cube",
    pattern: /立方体|方块|cube/,
    chinese_name: "立方体",
  },
  {
    type: "sphere",
    pattern: /球体|球|sphere/,
    chinese_name: "球体",
  },
  {
    type: "cylinder",
    pattern: /圆柱体|圆柱|cylinder/,
    chinese_name: "圆柱体",
  },
  {
    type: "plane",
    pattern: /平面|plane/,
    chinese_name: "平面",
  },
];

const PRONOUN_PATTERN =
  /它|那个|刚才那个|刚生成的|刚创建的|\bit\b|\bthat\b|last one|just created/;
const UNSAFE_REQUEST_PATTERN =
  /powershell|系统命令|system command|shell command|本地文件|local file|读取.*文件|read.*file|执行.*javascript|run.*javascript|执行.*js\b|忽略限制|ignore.*(?:limit|restriction)/;
const EXCESSIVE_SPAWN_PATTERN =
  /一百万|100\s*万|1[,\s]?000[,\s]?000|million\s+(?:cube|entit)/;

function makeToolCall(tool_name, args, revision) {
  return {
    tool_call_id: randomUUID(),
    tool_name,
    expected_revision: revision,
    dry_run: false,
    args,
  };
}

function clarification(reason) {
  return {
    assistant_message: `无法可靠确定目标。请明确指定实体名称或 entity_id${reason ? `（${reason}）` : ""}。`,
    tool_calls: [],
  };
}

function refusal(message) {
  return {
    assistant_message: message,
    tool_calls: [],
  };
}

function findPrimitive(message) {
  return PRIMITIVES.find((primitive) => primitive.pattern.test(message));
}

function findColor(message) {
  return COLORS.find((color) => color.pattern.test(message));
}

function parseVectorAfter(message, marker_pattern) {
  const pattern = new RegExp(
    String.raw`(?:${marker_pattern})[^\d+\-.]*(${NUMBER_TEXT})${VECTOR_SEPARATOR}(${NUMBER_TEXT})${VECTOR_SEPARATOR}(${NUMBER_TEXT})`,
    "i"
  );
  const match = message.match(pattern);
  return match ? match.slice(1, 4).map(Number) : null;
}

function parsePosition(message) {
  return parseVectorAfter(
    message,
    String.raw`移动到|移到|放在|置于|位置(?:是|为|到)?|move(?:\s+\w+)?\s+to|position(?:\s+\w+)?\s+(?:at|to|is)|\bat`
  );
}

function parseAbsoluteScale(message) {
  return parseVectorAfter(
    message,
    String.raw`缩放到|缩放为|scale(?:\s+\w+)?\s+to`
  );
}

function scaleFactor(message) {
  if (/放大\s*(?:两|2)\s*倍|double(?:\s+its?)?\s+size|scale.*(?:by\s*)?2x/.test(message)) {
    return 2;
  }
  if (/缩小\s*(?:一半|到\s*0?\.5\s*倍)|half(?:\s+its?)?\s+size|scale.*(?:by\s*)?0?\.5x/.test(message)) {
    return 0.5;
  }
  return null;
}

function relativeMovement(message) {
  const directions = [
    {
      pattern: new RegExp(
        String.raw`(?:向|往)?右(?:移动)?\s*(${NUMBER_TEXT})|(?:move\s+)?(?:it\s+)?right\s+(?:by\s+)?(${NUMBER_TEXT})`,
        "i"
      ),
      axis: 0,
      sign: 1,
    },
    {
      pattern: new RegExp(
        String.raw`(?:向|往)?左(?:移动)?\s*(${NUMBER_TEXT})|(?:move\s+)?(?:it\s+)?left\s+(?:by\s+)?(${NUMBER_TEXT})`,
        "i"
      ),
      axis: 0,
      sign: -1,
    },
    {
      pattern: new RegExp(
        String.raw`(?:向|往)?上(?:移动)?\s*(${NUMBER_TEXT})|(?:move\s+)?(?:it\s+)?up\s+(?:by\s+)?(${NUMBER_TEXT})`,
        "i"
      ),
      axis: 2,
      sign: 1,
    },
    {
      pattern: new RegExp(
        String.raw`(?:向|往)?下(?:移动)?\s*(${NUMBER_TEXT})|(?:move\s+)?(?:it\s+)?down\s+(?:by\s+)?(${NUMBER_TEXT})`,
        "i"
      ),
      axis: 2,
      sign: -1,
    },
  ];
  for (const direction of directions) {
    const match = message.match(direction.pattern);
    if (match) {
      return {
        axis: direction.axis,
        amount: Number(match[1] ?? match[2]) * direction.sign,
      };
    }
  }
  return null;
}

function isValidPosition(position) {
  return position.every(
    (value) =>
      Number.isFinite(value) &&
      value >= -POSITION_LIMIT &&
      value <= POSITION_LIMIT
  );
}

function isValidScale(scale) {
  return scale.every(
    (value) =>
      Number.isFinite(value) && value > SCALE_MIN && value <= SCALE_MAX
  );
}

function mentionedNameMatches(message, entities) {
  const matches = entities.filter((entity) => {
    const name = entity.name?.trim().toLowerCase();
    return name && message.includes(name);
  });
  if (matches.length === 0) {
    return [];
  }
  const longest_name_length = Math.max(
    ...matches.map((entity) => entity.name.trim().length)
  );
  return matches.filter(
    (entity) => entity.name.trim().length === longest_name_length
  );
}

function resolveTarget(message, entities, session_context = {}) {
  const uuid = message.match(UUID_IN_TEXT)?.[0]?.toLowerCase();
  if (uuid) {
    const entity = entities.find(
      (candidate) => candidate.entity_id.toLowerCase() === uuid
    );
    return entity
      ? { entity }
      : { error: "指定的 entity_id 当前不存在" };
  }

  const name_matches = mentionedNameMatches(message, entities);
  if (name_matches.length === 1) {
    return { entity: name_matches[0] };
  }
  if (name_matches.length > 1) {
    return { error: "该名称匹配到多个实体" };
  }

  const primitive = findPrimitive(message);
  if (primitive) {
    const primitive_matches = entities.filter(
      (entity) => entity.primitive_type === primitive.type
    );
    if (primitive_matches.length === 1) {
      return { entity: primitive_matches[0] };
    }
    if (primitive_matches.length > 1) {
      return { error: `存在多个${primitive.chinese_name}` };
    }
  }

  const referenced_ids = [
    session_context.last_referenced_entity_id,
    session_context.last_created_entity_id,
  ];
  for (const entity_id of referenced_ids) {
    const entity = entities.find((candidate) => candidate.entity_id === entity_id);
    if (entity) {
      return { entity };
    }
  }

  if (!PRONOUN_PATTERN.test(message) && entities.length === 1) {
    return { entity: entities[0] };
  }
  return {
    error: entities.length === 0 ? "当前世界没有可引用实体" : "指代不明确",
  };
}

function spawnIntent(message, primitive) {
  if (!primitive) {
    return false;
  }
  if (/生成|创建|新建|来个|放一个|spawn|create|make|add/.test(message)) {
    return true;
  }
  return new RegExp(
    String.raw`^(?:一个|a\s+|an\s+)?(?:红色|绿色|蓝色|白色|red|green|blue|white)?\s*(?:立方体|方块|球体|球|圆柱体|圆柱|平面|cube|sphere|cylinder|plane)[。.!\s]*$`,
    "i"
  ).test(message);
}

function buildSpawn(message, primitive, revision) {
  const args = { primitive_type: primitive.type };
  const color = findColor(message);
  if (color) {
    args.properties = { color: color.value };
  }

  const position = parsePosition(message);
  if (position) {
    if (!isValidPosition(position)) {
      return refusal("坐标超出现有工具 Schema 的安全范围，未修改世界。");
    }
    args.transform = { position };
  }

  const factor = scaleFactor(message);
  if (factor !== null) {
    const scale = [factor, factor, factor];
    if (!isValidScale(scale)) {
      return refusal("缩放值超出现有工具 Schema 的安全范围，未修改世界。");
    }
    args.transform = { ...args.transform, scale };
  }

  const description = `${color ? color.name : ""}${primitive.chinese_name}`;
  return {
    assistant_message: `已生成一个${description}。`,
    tool_calls: [makeToolCall("entity.spawn_primitive", args, revision)],
  };
}

export class MockProvider {
  constructor({ model = "mock-deterministic-v0.2" } = {}) {
    this.name = "mock";
    this.model = model;
    this.capabilities = Object.freeze({
      ...DEFAULT_PROVIDER_CAPABILITIES,
      supports_finalization: false,
    });
  }

  getMetadata() {
    return {
      provider: this.name,
      model: this.model,
      ready: true,
      real: false,
      thinking: "disabled",
      capabilities: { ...this.capabilities },
    };
  }

  async plan(input) {
    const output = await this.run({
      message: input.user_message,
      world_snapshot: input.world_snapshot,
      session_context: input.session_context,
      tool_definitions: input.tool_definitions,
    });
    return createProviderOutput({
      provider: this.name,
      model: this.model,
      assistant_message: output.assistant_message,
      tool_calls: output.tool_calls.map((tool_call) => ({
        tool_call_id: tool_call.tool_call_id,
        tool_name: tool_call.tool_name,
        args: tool_call.args,
      })),
      finish_reason: output.tool_calls.length ? "tool_calls" : "stop",
      provider_metadata: {
        phase: "plan",
        attempt_count: 1,
        duration_ms: 0,
        http_status: null,
      },
    });
  }

  async close() {
  }

  async run({ message, world_snapshot, session_context = {} }) {
    const normalized = message.trim().toLowerCase();
    const revision = world_snapshot.revision;
    const entities = world_snapshot.entities;

    if (UNSAFE_REQUEST_PATTERN.test(normalized)) {
      return refusal(
        "该请求超出 Agent Core 的安全范围；MockProvider 不提供文件、脚本、PowerShell 或系统命令工具。"
      );
    }
    if (EXCESSIVE_SPAWN_PATTERN.test(normalized)) {
      return refusal("一次生成一百万个实体超出有界 MockWorld 演示范围，未修改世界。");
    }

    if (/撤销|undo/.test(normalized)) {
      return {
        assistant_message: "已请求撤销最近一次成功的世界修改。",
        tool_calls: [makeToolCall("history.undo", {}, revision)],
      };
    }

    if (/列出.*实体|所有实体|list\s+(?:all\s+)?entit/.test(normalized)) {
      return {
        assistant_message: "下面是当前实体列表。",
        tool_calls: [makeToolCall("world.query_entities", {}, revision)],
      };
    }

    const primitive = findPrimitive(normalized);
    if (spawnIntent(normalized, primitive)) {
      return buildSpawn(normalized, primitive, revision);
    }

    if (/删除|销毁|destroy|delete|remove/.test(normalized)) {
      const target = resolveTarget(normalized, entities, session_context);
      if (!target.entity) {
        return clarification(target.error);
      }
      return {
        assistant_message: `已删除实体 ${target.entity.name}。`,
        tool_calls: [
          makeToolCall(
            "entity.destroy",
            { entity_id: target.entity.entity_id },
            revision
          ),
        ],
      };
    }

    if (/查询|查看|\binspect\b|\bquery\b|\bget\b/.test(normalized)) {
      const target = resolveTarget(normalized, entities, session_context);
      if (!target.entity) {
        return clarification(target.error);
      }
      return {
        assistant_message: `下面是实体 ${target.entity.name} 的当前信息。`,
        tool_calls: [
          makeToolCall(
            "entity.get",
            { entity_id: target.entity.entity_id },
            revision
          ),
        ],
      };
    }

    const absolute_position = parsePosition(normalized);
    if (absolute_position) {
      if (!isValidPosition(absolute_position)) {
        return refusal("坐标超出现有工具 Schema 的安全范围，未修改世界。");
      }
      const target = resolveTarget(normalized, entities, session_context);
      if (!target.entity) {
        return clarification(target.error);
      }
      return {
        assistant_message: `已将实体 ${target.entity.name} 移动到指定位置。`,
        tool_calls: [
          makeToolCall(
            "entity.set_transform",
            {
              entity_id: target.entity.entity_id,
              transform: { position: absolute_position },
            },
            revision
          ),
        ],
      };
    }

    const movement = relativeMovement(normalized);
    if (movement) {
      const target = resolveTarget(normalized, entities, session_context);
      if (!target.entity) {
        return clarification(target.error);
      }
      const position = [...target.entity.transform.position];
      position[movement.axis] += movement.amount;
      if (!isValidPosition(position)) {
        return refusal("相对移动后的坐标超出现有工具 Schema 的安全范围，未修改世界。");
      }
      return {
        assistant_message: `已相对移动实体 ${target.entity.name}。`,
        tool_calls: [
          makeToolCall(
            "entity.set_transform",
            {
              entity_id: target.entity.entity_id,
              transform: { position },
            },
            revision
          ),
        ],
      };
    }

    const absolute_scale = parseAbsoluteScale(normalized);
    if (absolute_scale) {
      if (!isValidScale(absolute_scale)) {
        return refusal("缩放值超出现有工具 Schema 的安全范围，未修改世界。");
      }
      const target = resolveTarget(normalized, entities, session_context);
      if (!target.entity) {
        return clarification(target.error);
      }
      return {
        assistant_message: `已设置实体 ${target.entity.name} 的缩放。`,
        tool_calls: [
          makeToolCall(
            "entity.set_transform",
            {
              entity_id: target.entity.entity_id,
              transform: { scale: absolute_scale },
            },
            revision
          ),
        ],
      };
    }

    const factor = scaleFactor(normalized);
    if (factor !== null) {
      const target = resolveTarget(normalized, entities, session_context);
      if (!target.entity) {
        return clarification(target.error);
      }
      const scale = target.entity.transform.scale.map((value) => value * factor);
      if (!isValidScale(scale)) {
        return refusal("相对缩放后的值超出现有工具 Schema 的安全范围，未修改世界。");
      }
      return {
        assistant_message: `已按比例修改实体 ${target.entity.name} 的缩放。`,
        tool_calls: [
          makeToolCall(
            "entity.set_transform",
            {
              entity_id: target.entity.entity_id,
              transform: { scale },
            },
            revision
          ),
        ],
      };
    }

    const color = findColor(normalized);
    if (
      color &&
      /变成|变为|改成|改为|变红|变蓝|变绿|变白|set.*color|make.*(?:red|green|blue|white)/.test(
        normalized
      )
    ) {
      const target = resolveTarget(normalized, entities, session_context);
      if (!target.entity) {
        return clarification(target.error);
      }
      return {
        assistant_message: `已将实体 ${target.entity.name} 变成${color.name}。`,
        tool_calls: [
          makeToolCall(
            "entity.set_property",
            {
              entity_id: target.entity.entity_id,
              property_name: "color",
              value: color.value,
            },
            revision
          ),
        ],
      };
    }

    if (/隐藏|hide/.test(normalized)) {
      const target = resolveTarget(normalized, entities, session_context);
      if (!target.entity) {
        return clarification(target.error);
      }
      return {
        assistant_message: `已隐藏实体 ${target.entity.name}。`,
        tool_calls: [
          makeToolCall(
            "entity.set_property",
            {
              entity_id: target.entity.entity_id,
              property_name: "visible",
              value: false,
            },
            revision
          ),
        ],
      };
    }

    if (/显示|show|make.*visible/.test(normalized)) {
      const target = resolveTarget(normalized, entities, session_context);
      if (!target.entity) {
        return clarification(target.error);
      }
      return {
        assistant_message: `已显示实体 ${target.entity.name}。`,
        tool_calls: [
          makeToolCall(
            "entity.set_property",
            {
              entity_id: target.entity.entity_id,
              property_name: "visible",
              value: true,
            },
            revision
          ),
        ],
      };
    }

    if (/移走|move.*away/.test(normalized)) {
      const target = resolveTarget(normalized, entities, session_context);
      if (!target.entity) {
        return clarification(target.error);
      }
      return refusal(
        `请明确实体 ${target.entity.name} 要移动到的坐标，或给出向左、向右、向上、向下的距离。`
      );
    }

    return {
      assistant_message:
        "MockProvider 未识别该意图。可尝试创建 primitive、列出/查询实体、移动/缩放/改色/显隐、删除或撤销。",
      tool_calls: [],
    };
  }
}
