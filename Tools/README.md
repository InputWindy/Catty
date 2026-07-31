# Tools

Engine-local tooling. Root only exposes thin user-facing `.bat` launchers.

| File | Role |
|------|------|
| `maho_python.bat` | Run a script with `Tools/python/python.exe` (not on PATH) |
| `maho_pythonw.bat` | Same with `pythonw.exe` (no console; for GUI tools) |
| `launch_create_project.vbs` / `launch_package.vbs` | WScript → pythonw for create / package GUIs |
| `create_project.py` | New-project UI (`createProject.bat`) |
| `generateProject.py` / `generateProject.bat` | `.cproject` / workspace → sibling `.sln` |
| `package_ui.py` / `package.bat` | Packaging UI (logs in window; abort via Close) |
| `package.py` | Headless CLI package |
| `clean.py` | Wipe generated/temp files (`clean.bat` at engine root) |
| `object_reflect_codegen.py` / `.bat` | Scan `MAHO_OBJECT` → ObjectReflectTypes.gen.* |
| `scan_plugins.py` / `scan_plugins.bat` | Scan `.cplugin` → module DAG / build-order JSON |
| `reflect_codegen.bat` | Deprecated alias → `object_reflect_codegen.bat` |
| `maho_tools.py` | Shared helpers |

## Local Python

Installed by root `setup.bat` into `Tools/python/`（gitignore，含 `_cache/` 安装包缓存）。Do not put on PATH; always go through `maho_python.bat` / `maho_pythonw.bat` / `launch_*.vbs`. Users clone the repo then run `setup.bat` themselves.

All `Tools/*.py` scripts **refuse** a system Python at startup (`maho_tools.ensure_engine_python`). Launch only via the bats above.

Templates remain under `Build/Templates/GameProject/`.
