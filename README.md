# Catty

UE-style engine (**Catty** DLL) + tools to spawn game projects (`.cproject`, like `.uproject`).

## First-time setup

```bat
setup.bat
```

Installs a **private** Python under `Tools/python/`（不写系统 PATH，不进 git）。之后工具通过 `Tools/catty_python.bat` 调用它。

```bat
setup.bat --force   # 损坏时强制重装
```

## Root scripts（用户入口）

| Script | Role |
|--------|------|
| `setup.bat` | 安装引擎局部 Python（`Tools/python`） |
| `createProject.bat` | UI：新建游戏项目 / 注册 `.cproject` 双击 |
| `generateProject.bat` | 根据 `.cproject`（或引擎工作区）生成同级 `.sln` |
| `package.bat` | 打开打包 UI → `Packaged/<Platform>/` |

内部（不必看根目录）：`Tools/clean.bat`、`Tools/reflect_codegen.bat`、`Tools/catty_python.bat` 等。

```bat
setup.bat
createProject.bat
generateProject.bat
package.bat
package.bat D:\Games\MyGame\MyGame.cproject
```

游戏项目根目录另有 `package.bat` / `clean.bat`（模板）：读 `.cproject` 的 `EngineDirectory`，用引擎局部 Python 唤起引擎 Tools。

## New project flow

1. `setup.bat`（首次）  
2. `createProject.bat` → 填项目名、父目录、引擎路径 → Create  
3. 得到 `Parent/Name/Name.cproject`  
4. 双击 `.cproject`（或 `generateProject.bat`）→ 同级 `.sln` → VS 打开  

## Layout

```text
Catty/  Test0/
Doc/
Build/
Tools/
  python/              # 局部 Python（gitignore，setup.bat 安装）
  _cache/              # 安装包缓存（gitignore）
  catty_python.bat
  clean.bat            # 引擎清理（内部）
  *.py
setup.bat  createProject.bat  generateProject.bat  package.bat
```

## Clean（内部）

```bat
Tools\clean.bat
Tools\clean.bat --ask
Tools\clean.bat --dry-run
```

会整夹删除 `Intermediate` / `Binaries` / `Packaged` / `Cached` / `Saved` 等；**不删** `Tools/python`。

## Engine workspace

`generateProject.bat`（无参数）：`cmake -S Build -B Intermediate`，并在仓库根写出 `CattyWorkspace.sln`。

## 引擎架构设计

- **看图：** [Doc/Engine/引擎架构设计.html](Doc/Engine/引擎架构设计.html)
- 文本稿：[Doc/Engine/引擎架构设计.md](Doc/Engine/引擎架构设计.md)
