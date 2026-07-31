# Catty — Agent instructions (first stop for every AI)

This file is the **vendor-neutral entry** for Claude Code, Codex, Copilot, Cursor, and any other agent.

## What this repo is

- **Catty** = UE-style C++ engine (`Catty/` DLL + `Build/` + `Tools/`). Naming/style align with Unreal habits; this is **not** an Unreal Engine source tree.
- **MyGame** (or other games) = separate project pointing at this engine via `.cproject`.
- **`Doc/`** = Catty documentation only (`Doc/Engine/`). Former UE learning-book HTML has been moved out of this repo.

## Read next (in order)

| Need | Go here |
|------|---------|
| Engine agent details | [`Doc/Engine/AGENTS.md`](Doc/Engine/AGENTS.md) |
| C++ coding standards (full text) | [`Doc/Engine/CODING_STANDARDS.md`](Doc/Engine/CODING_STANDARDS.md) |
| Progress, why, pitfalls | [`Doc/Engine/DESIGN_JOURNAL.md`](Doc/Engine/DESIGN_JOURNAL.md) |
| Architecture (human + AI) | [`Doc/Engine/引擎架构设计.html`](Doc/Engine/引擎架构设计.html) |
| Module law (RHI / Object / Extension) | `**/CONTRACT.md` next to that code (see table below) |
| Built-in extensions list | [`Catty/Plugins/README.md`](Catty/Plugins/README.md) |

## Repository map

```text
Catty/                          # this git root
├── AGENTS.md                   # YOU ARE HERE
├── CLAUDE.md                   # thin pointer → this file
├── Catty/                      # engine sources
│   ├── Source/Public/          # public headers (#include <...>)
│   ├── Source/Private/         # implementations (#include "...")
│   ├── Source/Generated/       # codegen — DO NOT hand-edit
│   ├── Plugins/                # optional .cplugin modules
│   └── ThirdParty/             # e.g. VMA header
├── Build/                      # CMake, game project template
├── Tools/                      # setup / generateProject / reflect codegen
├── Doc/
│   └── Engine/                 # Catty architecture + agent docs
└── .cursor/rules/              # Cursor projections (not sole source of truth)
```

## Hard invariants (summary)

Full text: [`Doc/Engine/CODING_STANDARDS.md`](Doc/Engine/CODING_STANDARDS.md) and module `CONTRACT.md`.

1. **Allman braces**, **Tab** indent, **English comments only** in `.h`/`.cpp`.
2. Naming: `U*` for UObject types, `F*` otherwise, `b` for bools, PascalCase members/functions.
3. Public includes `<...>`, Private `"..."`; no nested `Public/Catty/`.
4. Pooled `UObject` / `UPackage` / `UResource`: pass **`FObjectRef`** only — see Object `CONTRACT.md`.
5. Never hand-edit `Catty/Source/Generated/**`.
6. RHI: upper layers use **Manager** + logical **Graphics/Compute/Transfer** queues; no `vulkan.h` / VMA in Public — see RHI `CONTRACT.md`.

## Module contracts (status lives here too)

| Area | Contract | Journal section |
|------|----------|-----------------|
| RHI | [`Catty/Source/Public/Render/RHI/CONTRACT.md`](Catty/Source/Public/Render/RHI/CONTRACT.md) | DESIGN_JOURNAL → RHI |
| Object / GC refs | [`Catty/Source/Public/Core/Object/CONTRACT.md`](Catty/Source/Public/Core/Object/CONTRACT.md) | Object |
| Extensions | [`Catty/Source/Public/Core/Extension/CONTRACT.md`](Catty/Source/Public/Core/Extension/CONTRACT.md) | Extension |
| Editor UI | [`Catty/Source/Public/Core/Editor/CONTRACT.md`](Catty/Source/Public/Core/Editor/CONTRACT.md) | Editor UI |

**Where are we now?** Read [`Doc/Engine/DESIGN_JOURNAL.md`](Doc/Engine/DESIGN_JOURNAL.md) first, then the module `CONTRACT.md` `Status` / `Pitfalls` sections.

## Git

- Create commits when the user asks.
- **`git push` only after the user agrees** (project rule).

## Vendor adapters (thin pointers only)

| Tool | Adapter file |
|------|----------------|
| Any | **This `AGENTS.md`** |
| Claude Code | [`CLAUDE.md`](CLAUDE.md) |
| GitHub Copilot | [`.github/copilot-instructions.md`](.github/copilot-instructions.md) |
| Cursor | [`.cursor/rules/`](.cursor/rules/) — short; must not diverge from `Doc/Engine/` |
