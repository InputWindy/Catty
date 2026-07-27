# Catty Lua 脚本接入

> 运行时嵌入 **Lua 5.4** + **sol2** 绑定；游戏线程调度。符号以 `FScriptSystem` 为准。

## 角色

| 侧 | 做什么 |
|----|--------|
| C++ | 持有 VM、登记 `catty.*` API、在帧循环里 `Call("OnUpdate", dt)` |
| Lua | 写在已绑定 API 之上的游戏逻辑（`Scripts/main.lua` 等） |

## 生命周期

1. `FApp::InitializeEngine` → `ScriptSystem.Initialize(ProjectScriptsDir)`  
2. 若存在 `Scripts/main.lua` → `DoFile`  
3. 每帧 `Update` / `FixedUpdate` → `OnUpdate` / `OnFixedUpdate`（有则调）  
4. `Shutdown` → 销毁 VM  

## 内置绑定（`catty` 表）

完整函数参考（签名 / 参数 / 返回值 / 示例）：**[LuaAPI.html](./LuaAPI.html)**

- `log` / `log_warn` / `log_error(msg)`  
- `get_cvar_int/float/bool/string(name [, default])`  
- `set_cvar_int/float/bool/string(name, value)`  
- 帧回调：全局 `OnUpdate(dt)` / `OnFixedUpdate(fixedDt)`（可选）  


## 工程布局

- 配置：`FEngineConfig::ProjectScriptsDir`（默认 `"Scripts"`）  
- 模板 / 工程：`Scripts/main.lua`；CMake POST_BUILD / install 会拷贝  

## 源码

- 公开：`Catty/Script/ScriptSystem.h`  
- 实现：`Private/Script/ScriptSystem.cpp`  
- 依赖：`CattyDependencies.cmake`（Lua 静态库 + sol2 头文件）  
