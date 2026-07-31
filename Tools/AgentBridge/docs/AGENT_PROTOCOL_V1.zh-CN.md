# Maho Agent Protocol v1

[English](AGENT_PROTOCOL_V1.md)

## 1. 范围与约定

`protocol_version` 为 `"1.0"`。所有由 Maho 定义的对外 JSON 字段名均使用
`snake_case`。`additionalProperties` 等 JSON Schema 关键字保留标准拼写。

服务只监听回环地址。v1 仅操作内存中的 `MockWorld`；真实 C++ 世界对象或指针
不会通过本协议传输。

服务使用 `crypto.randomUUID()` 生成以下 ID：

- `request_id`
- `session_id`
- `world_id`
- `tool_call_id`
- `entity_id`
- `undo_token`

时间戳使用 Unix Epoch 毫秒数。

## 2. 标准 Envelope

标准 Envelope 如下：

```json
{
  "protocol_version": "1.0",
  "request_id": "b2894e08-a2ce-4db2-90dd-9362f98825a2",
  "session_id": "2d8f6082-9c9c-4791-8d5d-f2af00c1113f",
  "world_id": "3d73d0f1-123f-4824-ae4e-eae96397547b",
  "type": "tools.execute",
  "timestamp_ms": 1785312000000,
  "payload": {
    "tool_calls": []
  }
}
```

`POST /v1/tools/execute` 接受此标准格式。为了便于本地脚本调用，它也接受本文档
定义的扁平格式。`POST /v1/agent/run` 使用该 Endpoint 专用的精简请求体。

不受支持的协议版本会返回 `UNSUPPORTED_PROTOCOL_VERSION`。

## 3. Session 与 World

`POST /v1/sessions` 会创建一个 Session 及其默认 MockWorld：

```json
{
  "ok": true,
  "protocol_version": "1.0",
  "session_id": "2d8f6082-9c9c-4791-8d5d-f2af00c1113f",
  "world_id": "3d73d0f1-123f-4824-ae4e-eae96397547b",
  "world_revision": 0,
  "error": null
}
```

Session 和 World 都是进程本地的内存对象。v0.1 中，每个 Session 只拥有一个
MockWorld。

World Snapshot 的结构如下：

```json
{
  "world_id": "3d73d0f1-123f-4824-ae4e-eae96397547b",
  "revision": 1,
  "entities": [],
  "history": []
}
```

Entity 包含：

```json
{
  "entity_id": "3402b82a-6d81-44c8-819a-70bea25907ca",
  "generation": 1,
  "name": "cube_1",
  "entity_type": "primitive",
  "primitive_type": "cube",
  "transform": {
    "position": [0, 0, 0],
    "rotation": [0, 0, 0],
    "scale": [1, 1, 1]
  },
  "properties": {
    "color": [1, 1, 1, 1],
    "visible": true,
    "label": ""
  }
}
```

限制：

- `primitive_type`：`cube`、`sphere`、`cylinder`、`plane`
- `position`：三个有限数值，范围为 -100000 到 100000
- `rotation`：三个有限欧拉角数值，范围为 -360000 到 360000
- `scale`：三个有限数值，必须大于 0.0001 且不超过 10000
- `color`：四个 0 到 1 之间的有限数值（RGBA）
- `label`：最多 256 个字符
- 不允许任意扩展属性

## 4. 工具定义

每个 Registry 条目均包含 `name`、`description`、JSON `schema`、
`mutates_world`、`undoable` 和 `execute`。所有参数 Schema 都使用
`additionalProperties: false`。

| 工具 | 修改 World | 可撤销 | 参数 |
| --- | --- | --- | --- |
| `world.get_summary` | 否 | 否 | `{}` |
| `world.query_entities` | 否 | 否 | 可选的精确匹配 `name`、`entity_type`、`primitive_type`、`limit` |
| `entity.get` | 否 | 否 | `entity_id` |
| `entity.spawn_primitive` | 是 | 是 | `primitive_type`；可选的 `name`、部分 `transform`、白名单内的 `properties` |
| `entity.set_transform` | 是 | 是 | `entity_id`、非空的部分 `transform` |
| `entity.set_property` | 是 | 是 | `entity_id`、`property_name`、类型正确的 `value` |
| `entity.destroy` | 是 | 是 | `entity_id` |
| `history.undo` | 是 | 否 | 可选的 `undo_token`；默认使用最近一个可用 Token |

`entity.set_property.property_name` 仅允许 `color`、`visible` 和 `label`。

## 5. ToolCall 与 ToolResult

ToolCall：

```json
{
  "tool_call_id": "6d3411b8-020e-423a-a1e4-5eb610c69a91",
  "tool_name": "entity.spawn_primitive",
  "expected_revision": 0,
  "dry_run": false,
  "args": {
    "primitive_type": "cube",
    "properties": {
      "color": [1, 0, 0, 1]
    }
  }
}
```

成功的 ToolResult：

```json
{
  "ok": true,
  "request_id": "b2894e08-a2ce-4db2-90dd-9362f98825a2",
  "tool_call_id": "6d3411b8-020e-423a-a1e4-5eb610c69a91",
  "before_revision": 0,
  "after_revision": 1,
  "changes": [
    {
      "operation": "spawn",
      "entity_id": "3402b82a-6d81-44c8-819a-70bea25907ca",
      "before": null,
      "after": {}
    }
  ],
  "undo_token": "3a797fd1-7827-40cb-bd7e-b037d47e9346",
  "error": null,
  "data": {},
  "dry_run": false
}
```

`changes` 包含结构化的 before/after 数据。`data` 包含工具读取结果或操作后生成的
Entity。

## 6. 错误模型

稳定错误格式如下：

```json
{
  "code": "INVALID_ARGUMENT",
  "message": "Invalid arguments for tool entity.spawn_primitive",
  "details": {},
  "retryable": false
}
```

已定义的错误码：

- `INVALID_REQUEST`
- `UNSUPPORTED_PROTOCOL_VERSION`
- `UNKNOWN_SESSION`
- `UNKNOWN_WORLD`
- `UNKNOWN_TOOL`
- `INVALID_ARGUMENT`
- `ENTITY_NOT_FOUND`
- `REVISION_CONFLICT`
- `PERMISSION_DENIED`
- `MODEL_OUTPUT_INVALID`
- `EXECUTION_FAILED`
- `UNDO_NOT_AVAILABLE`
- `REQUEST_TOO_LARGE`
- `BUSY`
- `TIMEOUT`
- `INTERNAL_ERROR`

常见 HTTP 状态码映射：

- 400：请求格式或参数无效
- 403：权限不足
- 404：未知资源、工具或 Entity
- 409：revision 冲突或服务忙
- 413：请求体过大
- 500：执行失败或内部错误
- 504：超时

## 7. Revision 与幂等

- 只读事务不会改变 `world_revision`。
- 包含一个或多个写操作的成功事务只会将 revision 增加一次。
- 失败事务不会改变 revision。
- `dry_run: true` 会在临时状态上完成协议、revision、Registry、Schema 和执行
  校验，返回预计 changes，但不创建 UndoRecord，也不改变 revision。
- 同一批次的所有 ToolCall 必须使用相同的 `expected_revision` 和 `dry_run` 值。
- revision 不匹配时，会在修改 World 之前返回 `REVISION_CONFLICT`。

`request_id` 在所属 Session 内具有幂等性。重复提交已完成的请求时，服务返回
第一次执行结果并设置 `"replayed": true`，不会再次执行。并发提交相同请求时，
后续请求会等待第一次请求完成。相同 Session 正在执行其他请求时，新请求可能
收到可重试的 `BUSY` 错误。

## 8. 批量事务

`POST /v1/tools/execute` 接受 `tool_calls` 数组。批次默认以原子事务执行：

1. 校验每个 ToolCall 及其参数；
2. 对 MockWorld 和 UndoJournal 状态进行结构化克隆；
3. 按数组顺序执行工具；
4. 所有修改一起提交；如果任意调用失败，则恢复全部 World/Journal 状态；
5. 成功的写批次只增加一次 revision，并生成一个 `undo_token`。

失败时，`failed_tool_call_index` 表示失败调用的数组下标。之前执行过的
ToolResult 会被标记为 `rolled_back: true`。`history.undo` 必须作为事务中的
唯一调用执行。

扁平请求示例：

```json
{
  "protocol_version": "1.0",
  "request_id": "b2894e08-a2ce-4db2-90dd-9362f98825a2",
  "session_id": "2d8f6082-9c9c-4791-8d5d-f2af00c1113f",
  "world_id": "3d73d0f1-123f-4824-ae4e-eae96397547b",
  "tool_calls": [
    {
      "tool_call_id": "6d3411b8-020e-423a-a1e4-5eb610c69a91",
      "tool_name": "entity.spawn_primitive",
      "expected_revision": 0,
      "dry_run": false,
      "args": {
        "primitive_type": "cube"
      }
    }
  ]
}
```

批量响应：

```json
{
  "ok": true,
  "request_id": "b2894e08-a2ce-4db2-90dd-9362f98825a2",
  "tool_results": [],
  "world_revision": 1,
  "undo_token": "3a797fd1-7827-40cb-bd7e-b037d47e9346",
  "error": null,
  "replayed": false
}
```

## 9. Undo

v0.1 只能撤销某个 World 最近一次成功的写事务。Undo 成功后，该 Token 不可再次
使用。Undo 会：

- 恢复事务执行前的完整 MockWorld 状态；
- 支持撤销 spawn、transform、property、destroy 和完整批次；
- 增加到一个**新的** revision，而不是简单地回退 revision；
- 对缺失、过期或已使用的 Token 返回 `UNDO_NOT_AVAILABLE`。

UndoRecord 当前保存事务执行前 MockWorld 的结构化克隆。这是仅适用于 MockWorld
的临时策略。未来的 C++ Adapter 必须用引擎原生的稳定 Entity Handle、
ChangeSet、事务边界和 Undo 应用机制替换此实现。

## 10. Endpoint

### `GET /v1/health`

返回：

```json
{
  "ok": true,
  "protocol_version": "1.0",
  "mock": true,
  "busy": false,
  "status": "mock (no CURSOR_API_KEY)"
}
```

### `POST /v1/sessions`

请求体可以是 `{}` 或 `{ "protocol_version": "1.0" }`。响应使用 HTTP 201，并
返回新建的 Session/World ID。

### `POST /v1/agent/run`

请求：

```json
{
  "session_id": "2d8f6082-9c9c-4791-8d5d-f2af00c1113f",
  "request_id": "b2894e08-a2ce-4db2-90dd-9362f98825a2",
  "message": "生成一个红色立方体",
  "expected_revision": 0
}
```

可选字段为 `protocol_version` 和 `world_id`。

响应：

```json
{
  "ok": true,
  "request_id": "b2894e08-a2ce-4db2-90dd-9362f98825a2",
  "assistant_message": "已生成一个红色立方体。",
  "tool_results": [],
  "world_revision": 1,
  "undo_token": "3a797fd1-7827-40cb-bd7e-b037d47e9346",
  "error": null,
  "replayed": false
}
```

Provider 文本不具备权威性。如果任意工具失败，AgentService 会用基于
CommandExecutor 结果的失败消息替换 Provider 文本。

### `POST /v1/tools/execute`

接受标准 `tools.execute` Envelope，或者第 8 节所示的扁平批量格式。返回批量
响应和 ToolResult。

### `GET /v1/world/snapshot`

查询参数：

```text
/v1/world/snapshot?session_id=<uuid>&world_id=<optional-uuid>
```

响应：

```json
{
  "ok": true,
  "protocol_version": "1.0",
  "snapshot": {},
  "error": null
}
```

### `POST /v1/history/undo`

请求：

```json
{
  "session_id": "2d8f6082-9c9c-4791-8d5d-f2af00c1113f",
  "request_id": "40644ffc-0e8a-4405-bba4-9dbd9d744996",
  "expected_revision": 1,
  "undo_token": "3a797fd1-7827-40cb-bd7e-b037d47e9346"
}
```

可以省略 `undo_token`，表示请求撤销最近一个可用事务。可选字段为
`protocol_version`、`world_id`、`tool_call_id` 和 `dry_run`。

### `GET /v1/events`

接受可选的 `after=<integer>` 参数，并返回与旧版 Endpoint 相同的有界事件流，
同时增加 `ok` 和 `protocol_version` 字段。

## 11. 旧版兼容性

以下 Endpoint 保留原有字段、状态码以及 `/chat` -> `/events` 异步流程：

- `GET /health`
- `POST /chat`
- `GET /events`
- `POST /shutdown`
