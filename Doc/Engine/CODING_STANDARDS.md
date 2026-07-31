# Catty C++ coding standards

Vendor-neutral **source of truth** for style. Cursor `.cursor/rules/ue-coding-style.mdc` must stay a short projection of this file.

Aligned with Unreal Engine habits. Applies to all Catty engine and game C++ in this ecosystem.

## Braces (Allman)

Opening `{` and closing `}` on their **own lines**, same column:

```cpp
namespace Catty
{
	class FGameApp
	{
	public:
		bool Initialize();
	};
}
```

Forbidden (K&R / Java style):

```cpp
namespace catty {
	class GameApp {
	};
}
```

Use Allman for `if` / `for` / `while` / `switch` / functions / classes / namespaces / multi-line lambdas.

## Naming

- `UObject` and subclasses: `U` prefix (`UObject`, `UPackage`, `UResource`, …)
- Other types: `F` prefix (`FApp`, `FConfig`, `FObjectRef`, `FRHIResourceManager`, …)
- Interfaces: `I` prefix (`IRHI`, `IEngineExtension`)
- Enums: `E` prefix (`ERHIQueueType`, `EExtensionPriority`)
- `bool` members/locals: `b` prefix (`bInitialized`)
- Members: PascalCase — **no** `m_` / `_` prefix
- Functions: PascalCase (`Initialize`, `Tick`)
- Namespaces: PascalCase (`Catty`)
- Macros / export: `CATTY_API`, `CATTY_EXPORTS`, …

## Headers vs sources

- **Templates**: implement in the header (in-class or bottom of header).
- **Non-templates**: prefer `.cpp`; headers mostly declarations.
- Short trivial accessors (`IsValid()`, one-line `return`) may stay in headers.
- `constexpr` functions stay in headers.

## Indent and types

- Indent with **Tab**
- Pointers/refs: `Type* Ptr`, `Type& Ref` (`*` / `&` with the type)
- Public headers under `Source/Public` (`Core/`, `Render/`, …) — **no** extra `Public/Catty/` nest
- Do not abuse `using namespace` in headers
- Game entry: `#include <Catty.h>` + `#include <EntryPoint.h>`, subclass `Catty::FApp`, implement `CreateApplication()`

## Include form

- **Public** headers (engine `Source/Public`, plugin `Source/*/Public`, shared Generated): `#include <...>`
- **Private** headers (`Source/Private`, plugin Private): `#include "..."`
- Third-party / system: angle brackets (`<imgui.h>`, `<vector>`, `<vulkan/vulkan.h>` only in Private)

## Comments

- All code comments (`//`, `/* */`, `/** */`) must be **English**
- Chinese explanations belong in `Doc/` or chat — **not** in `.h` / `.cpp`

## Related hard constraints

- Object refs: [`../../Catty/Source/Public/Core/Object/CONTRACT.md`](../../Catty/Source/Public/Core/Object/CONTRACT.md)
- RHI surface: [`../../Catty/Source/Public/Render/RHI/CONTRACT.md`](../../Catty/Source/Public/Render/RHI/CONTRACT.md)
