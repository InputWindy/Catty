# Catty

UE-style engine (**Catty** DLL) + tools to spawn game projects (`.cproject`, like `.uproject`).

## First-time setup

```bat
setup.bat
```

Installs a **private** Python under `Tools/python/`（不写系统 PATH，不进 git）。之后工具**只**通过 `Tools/catty_python.bat` / `catty_pythonw.bat` / `launch_*.vbs` 调用它；直接用系统 `python` 跑 `Tools/*.py` 会被拒绝。

```bat
setup.bat --force   # 损坏时强制重装
```

## Root scripts（用户入口）

| Script | Role |
|--------|------|
| `setup.bat` | 安装引擎局部 Python（`Tools/python`） |
| `createProject.bat` | UI：新建游戏项目 / 注册 `.cproject` 双击 |
| `clean.bat` | 清理 Intermediate / Binaries / Packaged / Cached / Saved 等 |

日常生成 `.sln`、打包、codegen 在 `Tools/`，不必在根目录感知：

| Internal | Role |
|----------|------|
| `Tools/generateProject.bat` | `.cproject` / 工作区 → `.sln`（也是双击 `.cproject` 的目标） |
| `Tools/package.bat` | 引擎侧打包 UI |
| `Tools/reflect_codegen.bat` | 反射目录 codegen |
| `Tools/catty_python.bat` | 指定局部解释器 |

```bat
setup.bat
createProject.bat
clean.bat
```

游戏项目根目录另有 `package.bat` / `clean.bat`（模板）：读 `.cproject` 的 `EngineDirectory`，用引擎局部 Python 唤起引擎 Tools。

## New project flow

1. `setup.bat`（首次）  
2. `createProject.bat` → 填项目名、父目录、引擎路径 → Create  
3. 得到 `Parent/Name/Name.cproject`  
4. 双击 `.cproject` → 同级 `.sln` → VS 打开  

## Layout

```text
Catty/  Test0/
Doc/
Build/
Tools/
  python/              # 局部 Python（gitignore，setup.bat 安装）
  _cache/              # 安装包缓存（gitignore）
  generateProject.bat / package.bat / clean.bat / …
setup.bat  createProject.bat  clean.bat
```

## Clean

```bat
clean.bat
clean.bat --ask
clean.bat --dry-run
```

会整夹删除 `Intermediate` / `Binaries` / `Packaged` / `Cached` / `Saved` 等；**不删** `Tools/python`。

## Engine workspace

`Tools\generateProject.bat`（无参数）：`cmake -S Build -B Intermediate`，并在仓库根写出 `CattyWorkspace.sln`。

## 引擎架构设计

- **看图：** [Doc/Engine/引擎架构设计.html](Doc/Engine/引擎架构设计.html)
- 文本稿：[Doc/Engine/引擎架构设计.md](Doc/Engine/引擎架构设计.md)
