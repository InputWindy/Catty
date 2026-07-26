# Tools

All Python tooling for Catty lives here. Repo root only exposes thin `.bat` launchers.

| File | Role |
|------|------|
| `setup.py` | New-project UI + `.cproject` association |
| `generateProject.py` | `.cproject` / workspace → sibling `.sln` |
| `package_ui.py` | Packaging UI (platform / config) |
| `package.py` | Headless CLI package (scripts / CI) |
| `clean.py` | Wipe generated/temp files, keep essentials |
| `catty_tools.py` | Shared helpers |

Templates remain under `Build/Templates/GameProject/`.
