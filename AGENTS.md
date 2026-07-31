# Maho — Agent instructions (first stop for every AI)

This file is the **vendor-neutral entry** for Claude Code, Codex, Copilot, Cursor, and any other agent.

## What this repo is

- **Maho** = UE-style C++ engine (`Maho/` DLL + `Build/` + `Tools/`). Naming/style align with Unreal habits; this is **not** an Unreal Engine source tree.
- **MyGame** (or other games) = separate project pointing at this engine via `.cproject`.
- **`Doc/`** = Maho documentation only (`Doc/Engine/`). Former UE learning-book HTML has been moved out of this repo.

## Read next (in order)

| Need | Go here |
|------|---------|
| Engine agent details | [`Doc/Engine/AGENTS.md`](Doc/Engine/AGENTS.md) |
| C++ coding standards (full text) | [`Doc/Engine/CODING_STANDARDS.md`](Doc/Engine/CODING_STANDARDS.md) |
| Progress, why, pitfalls | [`Doc/Engine/DESIGN_JOURNAL.md`](Doc/Engine/DESIGN_JOURNAL.md) |
| Architecture (human + AI) | [`Doc/Engine/引擎架构设计.html`](Doc/Engine/引擎架构设计.html) |
| Module law (RHI / Object / Extension) | `**/CONTRACT.md` next to that code (see table below) |
| Built-in extensions list | [`Maho/Plugins/README.md`](Maho/Plugins/README.md) |

## Repository map

```text
Maho/                          # this git root
├── AGENTS.md                   # YOU ARE HERE
├── CLAUDE.md                   # thin pointer → this file
├── Maho/                      # engine sources
│   ├── Source/Public/          # public headers (#include <...>)
│   ├── Source/Private/         # implementations (#include "...")
│   ├── Source/Generated/       # codegen — DO NOT hand-edit
│   ├── Plugins/                # optional .cplugin modules
│   └── ThirdParty/             # e.g. VMA header
├── Build/                      # CMake, game project template
├── Tools/                      # setup / generateProject / reflect codegen
├── Doc/
│   └── Engine/                 # Maho architecture + agent docs
└── .cursor/rules/              # Cursor projections (not sole source of truth)
```

## Hard invariants (summary)

Full text: [`Doc/Engine/CODING_STANDARDS.md`](Doc/Engine/CODING_STANDARDS.md) and module `CONTRACT.md`.

1. **Allman braces**, **Tab** indent, **English comments only** in `.h`/`.cpp`.
2. Naming: `U*` for UObject types, `F*` otherwise, `b` for bools, PascalCase members/functions.
3. Public includes `<...>`, Private `"..."`; no nested `Public/Maho/`.
4. Pooled `UObject` / `UPackage` / `UResource`: pass **`FObjectRef`** only — see Object `CONTRACT.md`.
5. `U*` Game assets (including `UTexture*`) are **CPU-only** — no FRHI/Vk on them; GPU via Render snapshot later.
6. Never hand-edit `Maho/Source/Generated/**`.
7. RHI: upper layers use **Manager** + logical **Graphics/Compute/Transfer** queues; no `vulkan.h` / VMA in Public — see RHI `CONTRACT.md`.

## Module contracts (status lives here too)

| Area | Contract | Journal section |
|------|----------|-----------------|
| RHI | [`Maho/Source/Public/Render/RHI/CONTRACT.md`](Maho/Source/Public/Render/RHI/CONTRACT.md) | DESIGN_JOURNAL → RHI |
| Object / GC refs | [`Maho/Source/Public/Core/Object/CONTRACT.md`](Maho/Source/Public/Core/Object/CONTRACT.md) | Object |
| Extensions | [`Maho/Source/Public/Core/Extension/CONTRACT.md`](Maho/Source/Public/Core/Extension/CONTRACT.md) | Extension |
| Editor UI | [`Maho/Source/Public/Core/Editor/CONTRACT.md`](Maho/Source/Public/Core/Editor/CONTRACT.md) | Editor UI |

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
