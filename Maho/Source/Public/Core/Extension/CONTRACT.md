# Engine extensions — module contract

## Role

`IEngineExtension` plugins and built-in `*System` types participate in `FApp` staged ticks (`EEngineStage`) with dependency ordering (`DependsPack` / TypeList).

## Invariants

- Priority enum: `System | Layer | Overlay` (`.cplugin` validates the same strings).
- Built-ins live in engine core and are registered from the game’s **generated** `*App.cpp`.
- Optional plugins: `Maho/Plugins/*.cplugin` or the game’s `Plugins/` — not linked into `Maho.dll` unless built as separate modules.

## Built-in systems (authoritative list)

See [`../../../../Plugins/README.md`](../../../../Plugins/README.md):

| Class | Name |
|-------|------|
| `FPlatformSystem` | Platform |
| `FRenderSystem` | Render |
| `FGCSystem` | GC |
| `FResourceSystem` | Resource |
| `FWorkerPoolSystem` | WorkerPool |
| `FScriptSystem` | Script |

## Render vs RHI thread

- `FRenderServer`: orchestration / stages (Game thread side of render).
- `FRHIServer`: owns `IRHI`, runs on thread `MahoRHI`, fence/submit helpers.

## Allowed callers

- `FApp`, generated app registration, plugin modules, editor/script layers

## Forbidden

- Reintroducing removed `IModule` / `EModuleStage` naming in new code
- Hand-editing Generated `*App.cpp` as the long-term source of truth (regenerate from `.cproject`)

## Status

- Extension/`*System` rename and RHIServer split are in place.
- Architecture HTML under `Doc/Engine/` may still lag — prefer this CONTRACT + Plugins README + code.

## Pitfalls

- Stale docs saying `FRenderSystemModule` / `Public/Maho/...` paths
- Stale architecture HTML under `Doc/Engine/` — prefer this CONTRACT + Plugins README + code

## Related files

- `Core/Extension/<Name>/<Name>.h` (built-in systems), `Core/Extension/Script/ScriptLayer.h`, `Core/Extension/Editor/EditorLayer.h`, `Core/Application/App.h`
- `Render/RenderServer.h`, `Render/RHI/RHIServer.h`
- Journal: `Doc/Engine/DESIGN_JOURNAL.md` → Extensions
