# Maho AgentBridge

[English](README.md)

AgentBridge 是一个仅监听回环地址的 Node.js 服务，供现有 Maho 编辑器的
Agent 面板使用。它既保留旧版聊天 API，也提供基于内存 `MockWorld` 的独立
Agent Core v0.1。

当前版本的 Agent Core **不会**连接 C++ 游戏世界，也不开放系统命令、文件工具、
Lua 执行、C++ 反射、指针、WebSocket、渲染、物理或多人联机能力。

## 架构

v0.1 的请求链路如下：

```text
自然语言
  -> AgentService
  -> MockProvider 或 CursorProvider
  -> 结构化 ToolCall
  -> ToolRegistry + Ajv 校验
  -> CommandExecutor
  -> MockWorld
  -> ChangeSet + UndoJournal
  -> HTTP 响应 + JSONL 审计日志
```

`server.mjs` 只负责读取配置、创建服务对象、注册 HTTP 路由、启动服务器和协调
shutdown。业务逻辑位于 `src/` 下。

项目保留了现有的 `@cursor/sdk` 依赖。1.0.26 版本提供 `customTools`；
`CursorProvider` 只使用这些回调捕获内部 ToolCall，并以 plan 模式运行 SDK。
回调本身不会修改 `MockWorld`。工具是否成功始终以 `CommandExecutor` 的执行结果
为准。

Ajv 是直接依赖，因为工具契约会以 JSON Schema 的形式发布和编译。v0.1 的所有
参数 Schema 都拒绝额外属性。

## 环境要求与安装

- Node.js **22.13 或更高版本**（`@cursor/sdk@1.0.26` 要求此版本）
- npm

```powershell
cd Tools\AgentBridge
npm install
npm test
```

所有测试均使用 `node:test`、随机回环端口、相互独立的 Session 和 MockProvider。
测试不需要 Cursor API Key、外部网络服务、C++ 游戏或 MyGame 检出目录。

## 运行

默认启动方式：

```powershell
cd Tools\AgentBridge
npm start
```

与旧版兼容的显式启动命令：

```powershell
node server.mjs --port 8765 --cwd C:\path\to\MyGame
```

强制使用行为确定的 Mock 模式：

```powershell
$env:MAHO_AGENT_MOCK = "1"
npm start
```

未提供 `CURSOR_API_KEY` 时会自动选择 Mock 模式。为兼容现有 C++ 客户端，
`--api-key` 和 `--api-key-file` 参数仍然保留。API Key 的优先级如下：

1. `--api-key`
2. `--api-key-file`
3. `CURSOR_API_KEY`

## 环境变量

| 变量 | 默认值 | 说明 |
| --- | --- | --- |
| `MAHO_AGENT_HOST` | `127.0.0.1` | 监听地址；只接受 `127.0.0.1` 和 `::1` |
| `MAHO_AGENT_PORT` | `8765` | 监听端口；`--port` 优先级更高 |
| `MAHO_AGENT_MOCK` | 自动 | 设置为 `1` 时强制使用 MockProvider |
| `MAHO_AGENT_DATA_DIR` | `Tools/AgentBridge/.runtime` | JSONL 审计日志和运行数据目录 |
| `CURSOR_API_KEY` | 空 | Cursor SDK Key；未提供时选择 Mock 模式 |

为了避免改变现有编辑器行为，旧版 Cursor SDK JSONL 存储仍位于
`<--cwd>/Saved/Agent/cursor-sdk-store`。Agent Core 审计数据使用
`MAHO_AGENT_DATA_DIR`。`Tools/AgentBridge/.runtime/` 已被 Git 忽略。

请求体默认限制为 1 MiB。服务器绝不会监听非回环地址。

## API

保持不变的旧版 API：

- `GET /health`
- `POST /chat`
- `GET /events?after=<id>`
- `POST /shutdown`

Agent Core v1 API：

- `GET /v1/health`
- `POST /v1/sessions`
- `POST /v1/agent/run`
- `POST /v1/tools/execute`
- `GET /v1/world/snapshot`
- `POST /v1/history/undo`
- `GET /v1/events`

完整的请求、响应、revision、幂等、批量事务和 undo 契约参见
[docs/AGENT_PROTOCOL_V1.zh-CN.md](docs/AGENT_PROTOCOL_V1.zh-CN.md)。

## Mock 模式快速验证

在一个 PowerShell 窗口中启动服务：

```powershell
$env:MAHO_AGENT_MOCK = "1"
$env:MAHO_AGENT_PORT = "8765"
npm start
```

在另一个 PowerShell 窗口中执行：

```powershell
$base = "http://127.0.0.1:8765"
$session = Invoke-RestMethod -Method Post -Uri "$base/v1/sessions" `
  -ContentType "application/json" -Body "{}"

$requestId = [guid]::NewGuid().ToString()
$runBody = @{
  session_id = $session.session_id
  request_id = $requestId
  message = "生成一个红色立方体"
  expected_revision = 0
} | ConvertTo-Json
$run = Invoke-RestMethod -Method Post -Uri "$base/v1/agent/run" `
  -ContentType "application/json" -Body $runBody

Invoke-RestMethod -Method Get `
  -Uri "$base/v1/world/snapshot?session_id=$($session.session_id)"

$undoBody = @{
  session_id = $session.session_id
  request_id = [guid]::NewGuid().ToString()
  expected_revision = $run.world_revision
  undo_token = $run.undo_token
} | ConvertTo-Json
Invoke-RestMethod -Method Post -Uri "$base/v1/history/undo" `
  -ContentType "application/json" -Body $undoBody

Invoke-RestMethod -Method Post -Uri "$base/shutdown" `
  -ContentType "application/json" -Body "{}"
```

## MockWorld 限制

- 所有状态都保存在内存中，进程退出后即消失。
- 每个 Session 只拥有一个 MockWorld。
- Entity 仅支持基本体：`cube`、`sphere`、`cylinder` 或 `plane`。
- 属性仅限 `color`、`visible` 和 `label`。
- rotation 使用三个欧拉角数值。
- Undo 仅支持最近一次成功的写事务。
- v0.1 的 UndoJournal 会保存事务执行前的完整 MockWorld 快照。这是临时实现；
  设计 C++ 接入时，必须用真实世界适配器和引擎原生的变更/撤销机制替换。
