# Doc/Engine — Agent guide (Catty engine development)

After the repo-root [`AGENTS.md`](../../AGENTS.md), use this folder for **engine** work.

`Doc/` in this repository is **Catty-only**. UE source-study books used to live under `Doc/` but have been moved elsewhere — do not search this tree for Lumen/Nanite learning HTML.

## Documents in this folder

| File | Role |
|------|------|
| [`CODING_STANDARDS.md`](CODING_STANDARDS.md) | Full C++ style (Allman, naming, includes, comments) |
| [`DESIGN_JOURNAL.md`](DESIGN_JOURNAL.md) | Subsystem status, design intent, pitfalls |
| [`引擎架构设计.html`](引擎架构设计.html) | Architecture overview (verify against current `Extension` / RHI code if stale) |
| [`ObjectReflectAPI.html`](ObjectReflectAPI.html) | Reflect C++ API |
| [`LuaAPI.html`](LuaAPI.html) | Lua `catty.*` API |

Module contracts live next to code (`Catty/Source/Public/**/CONTRACT.md`), including Editor UI: [`../../Catty/Source/Public/Core/Editor/CONTRACT.md`](../../Catty/Source/Public/Core/Editor/CONTRACT.md).

## Typical workflows

### Change engine C++

1. Root `AGENTS.md` → hard invariants  
2. `CODING_STANDARDS.md`  
3. Relevant `CONTRACT.md` under `Catty/Source/Public/...`  
4. Skim `DESIGN_JOURNAL.md` for that subsystem  
5. Edit Public/Private sources; rebuild game (e.g. MyGame Debug)

### Understand “how far is RHI / GC / Extensions?”

Open [`DESIGN_JOURNAL.md`](DESIGN_JOURNAL.md), then the module `CONTRACT.md`.

### Regenerate reflection / Lua bindings

Use engine Tools (`object_reflect_codegen`); do not edit `Catty/Source/Generated/` by hand.

## Built-in systems

Accurate table: [`Catty/Plugins/README.md`](../../Catty/Plugins/README.md)  
(`FPlatformSystem`, `FRenderSystem`, `FGCSystem`, `FResourceSystem`, `FWorkerPoolSystem`, `FScriptSystem`).
