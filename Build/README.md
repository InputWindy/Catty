# Build

CMake entry + modules + new-project templates (kept out of the repo root).

```text
Build/
  CMakeLists.txt       # engine workspace entry (cmake -S Build)
  CMakePresets.json
  CMake/               # MahoDirectories / MahoHelpers
  Templates/           # GameProject skeleton for Tools/create_project.py
```

Root bats（用户入口）→ `Tools/maho_python.bat`：

| Bat | Role |
|-----|------|
| `setup.bat` | installs `Tools/python` |
| `createProject.bat` | `Tools/create_project.py` |
| `clean.bat` | `Tools/clean.py` |

Internal（`Tools/`）：`generateProject.bat` / `package.bat` / `object_reflect_codegen.bat` 等。

Game project template ships root `package.bat` / `clean.bat` + `Tools/invoke_engine.ps1`（读 `.cproject` → 引擎局部 Python）。

Docs for agents: repository-root `AGENTS.md` → `Doc/Engine/`.
