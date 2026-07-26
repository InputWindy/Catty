# Catty

UE-style engine (**Catty** DLL) + tools to spawn game projects (`.cproject`, like `.uproject`).

## Root scripts

| Script | Role |
|--------|------|
| `setup.py` | UI：新建项目 / 注册 `.cproject` 双击 |
| `generateProject.py` | 根据 `.cproject`（或引擎工作区）生成同级 `.sln` |
| `package.py` | 编译并输出到 `Packaged/<Platform>/` |

```bat
python setup.py
python generateProject.py
python generateProject.py D:\Games\MyGame\MyGame.cproject
python package.py D:\Games\MyGame\MyGame.cproject
```

## New project flow

1. `python setup.py` → 填项目名、父目录、引擎路径 → Create  
2. 得到 `Parent/Name/Name.cproject`（JSON）和源码模板  
3. 在 setup 里点 **Associate .cproject**（或稍后双击也会走 `generateProject.py`）  
4. 双击 `Name.cproject` → 同级生成 `Name.sln`  
5. 双击 `Name.sln` → Visual Studio  

`.cproject` 示例字段：`ProjectName`、`EngineDirectory`、`Description`、`Modules`。

## Layout

```text
Catty/  Test0/  Doc/     # 引擎与示例 / 文档
Build/                   # CMake + Python 工具 + 模板
Intermediate/ Binaries/ Cached/ Saved/ Packaged/   # 生成物（示例工作区）
setup.py  generateProject.py  package.py
CMakeLists.txt           # 引擎工作区（Catty + Test0）
```

## Engine workspace (optional)

不带参数时，`generateProject.py` 会为仓库根（Catty+Test0）生成 `CattyWorkspace.sln`。
