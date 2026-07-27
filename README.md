# Catty

UE-style engine (**Catty** DLL) + tools to spawn game projects (`.cproject`, like `.uproject`).

## Root scripts (bat only)

| Script | Role |
|--------|------|
| `setup.bat` | UI：新建项目 / 注册 `.cproject` 双击 |
| `generateProject.bat` | 根据 `.cproject`（或引擎工作区）生成同级 `.sln` |
| `package.bat` | 打开打包 UI（平台 / 配置 → `Packaged/<Platform>/`） |
| `clean.bat` | 一键清理临时/生成文件，只留项目必需 |

Python 在 `Tools/`；CMake 入口在 `Build/`（不放根目录）。

```bat
setup.bat
generateProject.bat
package.bat
package.bat D:\Games\MyGame\MyGame.cproject
clean.bat
```

游戏项目根目录也有 `package.bat`（由模板生成）：双击后读本地 `.cproject` 的 `EngineDirectory`，唤起引擎 `Tools/package_ui.py`。

## New project flow

1. `setup.bat` → 填项目名、父目录、引擎路径 → Create  
2. 得到 `Parent/Name/Name.cproject`  
3. 关联 `.cproject` → 双击生成同级 `.sln` → VS 打开  

## Layout

```text
Catty/  Test0/           # 引擎与示例
Doc/                     # 文档 + AGENTS.md（文档写作规范）
Build/                   # CMakeLists / presets / 模块 / 模板
Tools/                   # 全部 Python
Intermediate/ Binaries/ Cached/ Saved/ Packaged/
setup.bat  generateProject.bat  package.bat  clean.bat
```

## Clean

`clean.bat` 会**整夹删除** `Intermediate` / `Binaries` / `Packaged` / `Cached` / `Saved`，以及同级 `.sln` 等生成物（不再保留 README）。  
需要时由 `generateProject.bat` / 编译过程重新创建。  
不删：`Catty`、`Test0`、`Build`、`Tools`、`Doc`、源码与 bat。

```bat
clean.bat          # 默认直接清理，不再询问
clean.bat --ask    # 需要确认再删
clean.bat --dry-run
```

## Engine workspace

`generateProject.bat`（无参数）执行：`cmake -S Build -B Intermediate`，并在仓库根写出 `CattyWorkspace.sln`。

## 引擎架构设计

运行时模块关系（App / GC / ResourceManager / Package / FObject）：

- **看图（推荐）：** [Doc/Engine/引擎架构设计.html](Doc/Engine/引擎架构设计.html)
- 文本稿：[Doc/Engine/引擎架构设计.md](Doc/Engine/引擎架构设计.md)
