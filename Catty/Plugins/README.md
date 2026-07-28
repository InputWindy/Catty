# Catty Engine Plugins

Drop engine-wide plugins here. Each plugin is a folder with a `.cplugin` manifest
(JSON, UTF-8). Game-specific plugins belong in the game project's `Plugins/`.

**Convention:** one feature = one plugin = one module. `Catty.dll` is the engine
core (`FApp` + Log / Console / Timer / WorkerPool / ScriptSystem).

`FResourceServer` is a private implementation detail of the **CResourceManager**
plugin (compiled into that DLL), not a separate plugin.

## Naming

| Layer | Rule | Example |
|-------|------|---------|
| Plugin folder / `.cplugin` / CMake target / DLL / `GetName()` | `C` + subsystem | `CResourceManager` |
| Module class | `F` + role + `Module` | `class FResourceModule` |
| Module headers | `*Module.h` / `*Module.cpp` | `#include "ResourceModule.h"` |
| Export macros | per-plugin `Public/<CName>Api.h` | `#include "CResourceManagerApi.h"` |

Built-in plugins: `CPlatformWindow`, `CRenderServer`, `CImGuiSystem`,
`CGCManager`, `CResourceManager`.

## Game `.cproject` Plugins list

```json
"Plugins": [
  { "Name": "CPlatformWindow", "Enabled": true },
  { "Name": "CResourceManager", "Enabled": true }
]
```

Module dependency cycles / missing deps → **FATAL** at configure.

## Layout

```text
Catty/Plugins/<CName>/
  <CName>.cplugin
  Source/<CName>/
    Public/<X>Module.h
    Private/<X>Module.cpp
```

## Scan tool

```text
Tools\scan_plugins.bat --cproject path\to\Game.cproject
```
