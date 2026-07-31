# Catty design journal

Living status for **other AIs and humans**: what exists, why, known pitfalls, and what is still missing.  
Update this when a subsystem meaningfully changes. Module-level detail also lives in each `CONTRACT.md`.

---

## RHI (`Catty/Source/Public/Render/RHI/`)

**Status (2026-07):** Public abstraction + Vulkan skeleton shipped; clear/ImGui path still works via `GetVk*`.

- **Done**
  - Public: `RHIEnums.h`, `RHIResources.h`, `RHICommandList.h`, `RHIResourceManager.h`, extended `IRHI`
  - Logical queues always: `GetGraphicsQueue()` / `GetComputeQueue()` / `GetTransferQueue()` (never null)
  - Transfer may map to Graphics (or Compute) when no dedicated TRANSFER family — upper layer must not branch on hardware
  - VMA via vendored `Catty/ThirdParty/VulkanMemoryAllocator` + `FVulkanMemoryAllocator`
  - Manager `Acquire*` / `Release` with Desc free-list; Create* used by Manager
  - Command list record + queue `Submit` skeleton (CopyBuffer, barriers, Draw/Dispatch stubs)
- **Not done**
  - Full graphics/compute PSO creation (placeholder pipelines with null `VkPipeline` OK for compile)
  - Full `CopyBufferToTexture` regions / texture upload demo
  - Migrating ImGui off `ImGui_ImplVulkan` + `GetVk*`
- **Why**
  - Upper layers need backend-agnostic resources and G/C/T as **API promises** for later PC texture copies on Transfer
- **Pitfalls**
  - Win32 macros rename `CreateSemaphore` → use `CreateGpuSemaphore` / `DestroyGpuSemaphore`
  - `FVulkanRHI::GetVkGraphicsQueue()` is the Vk escape hatch; `IRHI::GetGraphicsQueue()` returns `FRHIQueue&`
  - Do not confuse external Unreal Engine study notes with this RHI — Catty RHI contracts live only under `Catty/Source/Public/Render/RHI/`
- **Key files**
  - Public: `RHI.h`, `RHI*`, `CONTRACT.md`
  - Private: `VulkanRHI.*`, `VulkanMemory.*`, `VulkanCommandList.*`, `VulkanResources.*`, `RHIResourceManager.cpp`
- **Next**
  - Real Hello-Triangle PSO + Transfer upload path using only Public APIs

Contract: [`../../Catty/Source/Public/Render/RHI/CONTRACT.md`](../../Catty/Source/Public/Render/RHI/CONTRACT.md)

---

## Extensions / app frame

**Status:** Built-ins are `*System` (`FPlatformSystem`, `FRenderSystem`, …), priority `System | Layer | Overlay`.  
`FRenderServer` orchestrates stages on Game; `FRHIServer` owns the RHI thread (`CattyRHI`).

- **Pitfalls**
  - Older docs may still say `F*Module` / `IModule` / `Public/Catty/` — trust code + [`Catty/Plugins/README.md`](../../Catty/Plugins/README.md)
  - Games register extensions from **generated** `Source/Generated/<Game>App.cpp` (regenerate via `.cproject`)

Contract: [`../../Catty/Source/Public/Core/Extension/CONTRACT.md`](../../Catty/Source/Public/Core/Extension/CONTRACT.md)

---

## Object / GC / refs

**Status:** Pool-allocated `UObject` graph; external handles are `FObjectRef`.

- **Pitfalls**
  - Never bare `AddRef` / `ReleaseRef` at call sites
  - Cycles: one side non-owning raw observer (no WeakRef type in current design)
  - `FObjectRef` / `FObjectWeakRef` naming in older notes — current rule is strong `FObjectRef` + raw observer for cycles (see Object CONTRACT)

Contract: [`../../Catty/Source/Public/Core/Object/CONTRACT.md`](../../Catty/Source/Public/Core/Object/CONTRACT.md)

---

## Resource system vs RHI Manager vs VMA

| Layer | Role |
|-------|------|
| `FResourceSystem` / `UResource` | Asset / UObject lifetime |
| `FRHIResourceManager` | GPU `FRHI*` objects, pooling |
| VMA | Device memory (Private only) |

Do not collapse these three.

---

## ImGui

Still **Dear ImGui official backends** (`ImGui_ImplGlfw` + `ImGui_ImplVulkan`), borrowing Vulkan handles from `FVulkanRHI::GetVk*`.  
Not on `FRHICommandList`. Planned migration later; do not “fix” in drive-by RHI work.

- **Multi-viewport enabled** (`ImGuiConfigFlags_ViewportsEnable`): undocked panels can leave the main OS window.
- Per-frame: after main-window ImGui submit, `FRHIServer::Flush()` then Game-thread `UpdatePlatformWindows` + `RenderPlatformWindowsDefault` (GLFW create must stay on main; Vulkan viewport work runs only while RHI is idle).
- Cost: KickRHI flushes RHI every frame when ImGui is up — acceptable for now; revisit if frame time regresses.

## Editor UI (`Core/Editor` + `FEditorLayer`)

Region registry for editor chrome contributions (menus, dual toolbars, dock panels, viewport overlays, blocking modals).

- **Law:** [`../../Catty/Source/Public/Core/Editor/CONTRACT.md`](../../Catty/Source/Public/Core/Editor/CONTRACT.md)
- Shell (`FEditorLayer`) owns geometry / DockSpace; `FEditorUIRegistry` owns contributions + Catalog separators
- Temporary Details = `DockPanel` + `bTransient` + `OpenDockPanel` — **not** Modal
- DockSpace uses `ImGuiDockNodeFlags_NoDockingOverCentralNode`; central keeps `NoTabBar|NoUndocking`
- Access: `TryGetEditorUIRegistry(FApp&)` or `GetExtension<FEditorLayer>()->GetUIRegistry()`
- Runtime game HUD is **out of scope** (future `FGameUI`)
- Debug menu: dummy pairs per region + Busy modal + Temporary Details sample

---

## How to update this journal

When you finish a meaningful slice: update **Status / Done / Not done / Pitfalls / Next** for that section, and mirror a one-line Status in the module `CONTRACT.md`.
