# Tools

Engine-local tooling. Root only exposes thin user-facing `.bat` launchers.

| File | Role |
|------|------|
| `catty_python.bat` | Run a script with `Tools/python/python.exe` (not on PATH) |
| `catty_pythonw.bat` | Same with `pythonw.exe` (no console; for GUI tools) |
| `create_project.py` | New-project UI (`createProject.bat`) |
| `generateProject.py` / `generateProject.bat` | `.cproject` / workspace → sibling `.sln` |
| `package_ui.py` / `package.bat` | Packaging UI |
| `package.py` | Headless CLI package |
| `clean.py` | Wipe generated/temp files (`clean.bat` at engine root) |
| `reflect_codegen.py` / `reflect_codegen.bat` | Scan `CATTY_REFLECT_*` → catalog |
| `catty_tools.py` | Shared helpers |

## Local Python

Installed by root `setup.bat` into `Tools/python/`（gitignore，含 `_cache/` 安装包缓存）。Do not put on PATH; always go through `catty_python.bat`. Users clone the repo then run `setup.bat` themselves.

Templates remain under `Build/Templates/GameProject/`.
