# Catty Engine Plugins

Optional runtime plugins live here (`.cplugin` + `Source/`). They are **not** part of
`Catty.dll`.

Built-in always-on modules ship inside the engine core instead:

| Module class | `GetName()` | Location |
|--------------|-------------|----------|
| `FPlatformModule` | `Platform` | `Source/Public/Core/Modules/` |
| `FRenderModule` | `Render` | (same) |
| `FGCModule` | `GC` | (same) |
| `FResourceModule` | `Resource` | (same) |

`FApp::RegisterModules()` registers those four. Game apps only need extra
`RegisterModule` calls for optional / project plugins.

Game-specific plugins belong in the game project's `Plugins/`.

## Optional plugin naming

| Layer | Rule | Example |
|-------|------|---------|
| Plugin folder / `.cplugin` / CMake target / DLL / `GetName()` | `C` + subsystem | `CMyFeature` |
| Module class | `F` + role + `Module` | `class FMyFeatureModule` |
| Module headers | Public path → `<>` | `#include <MyFeatureModule.h>` |
| Private headers | Private path → `""` | `#include "MyFeaturePrivate.h"` |
| Export macros | per-plugin `Public/<CName>Api.h` | `#include <CMyFeatureApi.h>` |

## Game `.cproject` Plugins list

```json
"Plugins": [
  { "Name": "CMyFeature", "Enabled": true }
]
```

Empty `"Plugins": []` means no optional plugin DLLs. Module dependency cycles /
missing deps → **FATAL** at configure.

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
