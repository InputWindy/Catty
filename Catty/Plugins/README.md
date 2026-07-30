# Catty Engine Plugins

Optional runtime plugins live here (`.cplugin` + `Source/`). They are **not** part of
`Catty.dll`.

Built-in always-on extensions ship inside the engine core. Games get a **generated**
`Source/Generated/<Game>App.cpp` (double-click `.cproject` / `generateProject`) that
calls `RegisterExtension` for builtins, the game Layer, and enabled plugins.

| Extension class | `GetName()` | Location |
|-----------------|-------------|----------|
| `FPlatform` | `Platform` | `Source/Public/Core/Extension/Platform.h` |
| `FRender` | `Render` | `Source/Public/Core/Extension/Render.h` |
| `FGC` | `GC` | `Source/Public/Core/Extension/GC.h` |
| `FResourceManager` | `Resource` | `Source/Public/Core/Extension/ResourceManager.h` |
| `FWorkerPoolModule` | `WorkerPool` | `Source/Public/Core/Extension/WorkerPool.h` |
| `FScript` | `Script` | `Source/Public/Core/Extension/Script.h` |

Game-specific plugins belong in the game project's `Plugins/`.

## Minimal example: `Sample`

```text
Catty/Plugins/Sample/
  Sample.cplugin
  Source/Sample/
    Public/SampleApi.h
    Public/SampleModule.h
    Private/SampleModule.cpp
```

`.cplugin` Module entry for auto-register:

```json
"Extension": {
  "Class": "Catty::FSampleModule",
  "Header": "SampleModule.h",
  "Priority": "Overlay"
}
```

1. Enable in the game `.cproject`: `{ "Name": "Sample", "Enabled": true }`
2. Double-click `.cproject` (or run `generateProject.bat`) — codegen injects Register + `#include`
3. Plugin `Source/<Module>/Public` is on the game include path via `Catty::Modules`

Omit `Extension` to build/link the DLL only (no App Register).

## Optional plugin naming

| Layer | Rule | Example |
|-------|------|---------|
| Plugin folder / `.cplugin` / CMake target / DLL / `GetName()` | short PascalCase name | `Sample` |
| Module class | `F` + role + `Module` | `class FSampleModule` |
| Module headers | Public path → `<>` | `#include <SampleModule.h>` |
| Private headers | Private path → `""` | `#include "SamplePrivate.h"` |
| Export macros | per-plugin `Public/<Name>Api.h` | `#include <SampleApi.h>` |

## Game `.cproject` Plugins list

```json
"Plugins": [
  { "Name": "Sample", "Enabled": true }
]
```

Empty `"Plugins": []` means no optional plugin DLLs (unless a plugin sets `EnabledByDefault: true`).

Extension stage order uses `TDependsPack` / `TDependsOn` in code — **not** a `.cplugin` field.
Do not put `Dependencies` on Modules.

## Layout

```text
Catty/Plugins/<Name>/
  <Name>.cplugin
  Source/<ModuleName>/
    Public/<X>Module.h
    Private/<X>Module.cpp
```

## Scan tool

```text
Tools\scan_plugins.bat --cproject path\to\Game.cproject
```
