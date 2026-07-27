# Build

CMake entry + modules + new-project templates (kept out of the repo root).

```text
Build/
  CMakeLists.txt       # engine workspace entry (cmake -S Build)
  CMakePresets.json
  CMake/               # CattyDirectories / CattyHelpers
  Templates/           # GameProject skeleton for Tools/setup.py
```

Root bats → `Tools/*.py`:

| Bat | Python |
|-----|--------|
| `setup.bat` | `Tools/setup.py` |
| `generateProject.bat` | `Tools/generateProject.py` |
| `package.bat` | `Tools/package_ui.py` |
| `clean.bat` | `Tools/clean.py` |

Game project template (`Templates/GameProject`) also ships root `package.bat` / `clean.bat`, which call `Tools/*_invoke.py` to resolve `EngineDirectory` from `*.cproject` and invoke the engine Tools scripts.

Docs agent rules: `Doc/AGENTS.md`.
