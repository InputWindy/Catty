# Build

CMake entry + modules + new-project templates (kept out of the repo root).

```text
Build/
  CMakeLists.txt       # engine workspace entry (cmake -S Build)
  CMakePresets.json
  CMake/               # CattyDirectories / CattyHelpers
  Templates/           # GameProject skeleton for Tools/create_project.py
```

Root bats → local Python via `Tools/catty_python.bat`:

| Bat | Python |
|-----|--------|
| `setup.bat` | installs `Tools/python`（no py script） |
| `createProject.bat` | `Tools/create_project.py` |
| `generateProject.bat` | `Tools/generateProject.py` |
| `package.bat` | `Tools/package_ui.py` |

Internal（`Tools/`）：`clean.bat` → `clean.py`，`reflect_codegen.bat` 等。

Game project template ships `package.bat` / `clean.bat` + `Tools/invoke_engine.ps1`（读 `.cproject` → 引擎 `catty_python.bat`）。

Docs agent rules: `Doc/AGENTS.md`.
