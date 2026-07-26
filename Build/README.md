# Build

CMake modules, Python tooling helpers, and new-project templates.

```text
Build/
  CMake/           # included by CMakeLists
  python/          # shared helpers for setup/generate/package
  Templates/       # GameProject skeleton for setup.py
```

Root entry scripts:

| Script | Role |
|--------|------|
| `setup.py` | UI: create new game + optional .cproject association |
| `generateProject.py` | `.cproject` / workspace → sibling `.sln` |
| `package.py` | Release build → `Packaged/<Platform>/` |
