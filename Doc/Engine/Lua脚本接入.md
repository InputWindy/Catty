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

手工 API（无 `CATTY_REFLECT_*`，见 `RegisterCoreBindings`）：

- `log` / `log_warn` / `log_error(msg)`  
- `get_cvar_int/float/bool/string(name [, default])`  
- `set_cvar_int/float/bool/string(name, value)`  
- 帧回调：全局 `OnUpdate(dt)` / `OnFixedUpdate(fixedDt)`（可选）  

反射 usertype 参考：**[LuaAPI.html](./LuaAPI.html)**（仅 `CATTY_REFLECT_*` 导出）。

### 反射生成的 usertype

与 C++ 共用 `CATTY_REFLECT_CLASS()`（写在类型上一行）；`Tools/reflect_codegen.bat` 生成绑定。

对象由 C++ 创建（`sol::no_constructor`）；Lua 侧主要调用已有实例上的方法。返回 `FObjectRef` 等复杂类型的接口暂不进 Lua（仍进 Reflect 元数据）。

Lua 名默认同自动转换：类型去 UE 前缀再 snake_case（`FObject` → `catty.object`），成员 PascalCase → snake_case（`GetName` → `get_name`）。可用 `CATTY_LUA_NAME` 覆盖。

示例：

```lua
-- obj 来自引擎侧推入的 object / resource 等
print(obj:get_name())
print(obj:get_ref_count())
```
目录：[ReflectCatalog.md](./ReflectCatalog.md)。


## 工程布局

- 配置：`FEngineConfig::ProjectScriptsDir`（默认 `"Scripts"`）  
- 模板 / 工程：`Scripts/main.lua`；CMake POST_BUILD / install 会拷贝  

## 源码

- 公开：`Catty/Script/ScriptSystem.h`  
- 实现：`Private/Script/ScriptSystem.cpp`  
- 依赖：`CattyDependencies.cmake`（Lua 静态库 + sol2 头文件）  
