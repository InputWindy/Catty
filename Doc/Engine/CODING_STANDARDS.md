# Maho C++ coding standards

Vendor-neutral **source of truth** for style. Cursor `.cursor/rules/ue-coding-style.mdc` must stay a short projection of this file.

Aligned with Unreal Engine habits. Applies to all Maho engine and game C++ in this ecosystem.

## Braces (Allman)

Opening `{` and closing `}` on their **own lines**, same column:

```cpp
namespace Maho
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
namespace maho {
	class GameApp {
	};
}
```

Use Allman for `if` / `for` / `while` / `switch` / functions / classes / namespaces / multi-line lambdas.

## Naming

- `UObject` and subclasses: `U` prefix (`UObject`, `UPackage`, `UResource`, …)
- Other types: `F` prefix (`FApp`, `FConfig`, `FObjectRef`, `FRHIResourceManager`, …)
- Interfaces: `I` prefix (`IRHI`, `IEngineExtension`)
- Enums: `E` prefix (`ERHIQueueType`, `ERHIResourceType`, `EExtensionPriority`)
- `bool` members/locals: `b` prefix (`bInitialized`)
- Members: PascalCase — **no** `m_` / `_` prefix
- Functions: PascalCase (`Initialize`, `Tick`)
- Namespaces: PascalCase (`Maho`)
- Macros / export: `MAHO_API`, `MAHO_EXPORTS`, …

### Kind must not replace Type (**mandatory**)

When naming a discriminator for “what sort of thing this is”, use **`Type`**, never **`Kind`**.

| Forbidden | Required |
|-----------|----------|
| `ERHIResourceKind` | `ERHIResourceType` |
| `GetKind()` | `GetType()` |
| member `Kind` meaning type tag | member `Type` |

Same rule for new enums / accessors / fields (`EFooKind`, `ResourceKind`, `Kind` as type tag). Do not introduce `Kind` as a synonym for `Type` to “sound nicer” or avoid a name clash — pick a clearer name (`EFooClass`, `EFooCategory`, …) if `Type` is already taken in that scope.

Allowed: unrelated English uses that are not a type-tag API (e.g. log string `"refusing menu …"`), and third-party / generated code you do not own.

## Headers vs sources

- **Templates**: implement in the header (in-class or bottom of header).
- **Non-templates**: prefer `.cpp`; headers mostly declarations.
- Short trivial accessors (`IsValid()`, one-line `return`) may stay in headers.
- `constexpr` functions stay in headers.

## Indent and types

- Indent with **Tab**
- Pointers/refs: `Type* Ptr`, `Type& Ref` (`*` / `&` with the type)
- Public headers under `Source/Public` (`Core/`, `Render/`, …) — **no** extra `Public/Maho/` nest
- Do not abuse `using namespace` in headers
- Game entry: `#include <Maho.h>` + `#include <EntryPoint.h>`, subclass `Maho::FApp`, implement `CreateApplication()`

## Include form

- **Public** headers (engine `Source/Public`, plugin `Source/*/Public`, shared Generated): `#include <...>`
- **Private** headers (`Source/Private`, plugin Private): `#include "..."`
- Third-party / system: angle brackets (`<imgui.h>`, `<vector>`, `<vulkan/vulkan.h>` only in Private)

## Comments

- All code comments (`//`, `/* */`, `/** */`) must be **English**
- Chinese explanations belong in `Doc/` or chat — **not** in `.h` / `.cpp`

## Related hard constraints

- Object refs: [`../../Maho/Source/Public/Core/Object/CONTRACT.md`](../../Maho/Source/Public/Core/Object/CONTRACT.md)
- RHI surface: [`../../Maho/Source/Public/Render/RHI/CONTRACT.md`](../../Maho/Source/Public/Render/RHI/CONTRACT.md)
