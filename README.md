# Catty

UE 风格 C++ 引擎（`Catty` DLL）+ 工具链：用 `.cproject`（类似 `.uproject`）创建、生成、打包游戏工程。

## 安装

首次克隆后在引擎根目录执行：

```bat
setup.bat
```

会在 `Tools/python/` 安装**引擎私有** Python（不写系统 PATH，不进 git）。之后所有工具只走局部解释器；直接用系统 `python` 跑 `Tools/*.py` 会被拒绝。

```bat
setup.bat --force   :: 损坏时强制重装
```

## 创建项目

```bat
createProject.bat
```

1. 填写项目名、父目录、引擎路径 → Create  
2. 得到 `Parent/Name/Name.cproject`  
3. 双击 `.cproject` → 生成同级 `.sln` → 用 Visual Studio 打开  

也可在引擎根用 `Tools\generateProject.bat`（无参数）生成引擎工作区 `CattyWorkspace.sln`。

## 打包

| 位置 | 入口 |
|------|------|
| 游戏工程根目录 | `package.bat`（读 `.cproject` 的 `EngineDirectory`，唤起引擎 Tools） |
| 引擎根目录 | `Tools\package.bat` |

GUI 可选平台 / 配置，产物在工程（或引擎）下的 `Packaged/<Platform>/`。关闭窗口可中止打包。

## 清理

```bat
clean.bat
clean.bat --ask
clean.bat --dry-run
```

会删除 `Intermediate` / `Binaries` / `Packaged` / `Cached` / `Saved`，以及 `Catty/Source/Generated/`（reflect / Lua codegen）。**不删** `Tools/python`。

---

## 目录结构

```text
Catty/                          # 引擎仓库根
├── setup.bat                   # 安装局部 Python
├── createProject.bat           # 新建游戏项目（GUI）
├── clean.bat                   # 清理中间产物
├── Catty/                      # 引擎模块（DLL 源码）
│   ├── Source/
│   │   ├── Public/             # 对外头文件
│   │   ├── Private/            # 实现
│   │   └── Generated/          # codegen 输出（gitignore，clean 可清）
│   ├── Shaders/
│   └── Plugins/
├── Build/                      # CMake 入口、模块、游戏工程模板
├── Tools/                      # 工具脚本 + 局部 python/
│   └── object_reflect_codegen.bat  # FObject 反射表生成
│   └── package.bat             # 打包 GUI
│   └── generateProject.bat     # .cproject / 工作区 → .sln
├── ThirdParty/                 # 第三方依赖
├── Doc/                        # 文档（HTML）
│   ├── Engine/                 # 引擎 API / 架构
│   └── …                       # UE 源码学习书（Nanite / Lumen / …）
└── README.md
```

游戏工程（由模板生成）大致为：

```text
MyGame/
├── MyGame.cproject
├── package.bat / clean.bat
├── Source/
├── Scripts/
├── Intermediate/               # VS / CMake 中间文件
├── Binaries/
└── Packaged/
```

---

## 文档

| 文档 | 说明 |
|------|------|
| [Doc/Engine/ObjectReflectAPI.html](Doc/Engine/ObjectReflectAPI.html) | Object / Struct / Enum 反射 C++ API（codegen 同步类型目录） |
| [Doc/Engine/LuaAPI.html](Doc/Engine/LuaAPI.html) | Lua API（手工 `catty.*`；Object usertype 另案） |
| [Doc/Engine/引擎架构设计.html](Doc/Engine/引擎架构设计.html) | 引擎架构（FApp / GC / Resource / Package 等） |
| [Doc/index.html](Doc/index.html) | UE 渲染源码解析文档集入口（可选阅读） |

用浏览器或 Cursor **Live Preview** 打开 HTML 即可。
