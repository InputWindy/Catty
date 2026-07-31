# Object / FObjectRef — module contract

## Role

Pool-allocated `UObject` graph (including `UPackage`, `UResource`). Lifetime is refcounted through **`FObjectRef`**.

## Invariants

- External / API boundaries pass **`FObjectRef`**, never raw owning pointers with manual `AddRef` / `ReleaseRef`.
- One handle type: `FObjectRef` (no `UPackageRef` template specializations).
- Subtypes: `Ref.Cast<UPackage>()` / `Ref.Cast<UResource>()`.
- `FObjectRef::Wrap(ptr)` is for engine / `SetReferencedObjects` backfill only.

## UPackage

- Catalog: `unordered_map<string, FObjectRef>`
- Outer links: `FObjectRef`; `GetOuter()` returns `FObjectRef`

## Cycles

Mutual strong refs leak. Break cycles with a **non-owning raw observer** pointer; re-`Wrap` only while the object is known alive.  
(Current design does not expose a separate WeakRef API for this.)

## Allowed callers

- GC, Resource system, Package, gameplay/object code that holds UObjects

## Forbidden

- Bare `AddRef` / `ReleaseRef` at call sites
- Storing long-lived owning raw `UObject*` without an `FObjectRef`

## Status

- Stable core pattern; keep CONTRACT and `.cursor/rules/fobject-ref.mdc` in sync when changing ref semantics.

## Pitfalls

- Older text may mention `FObjectWeakRef` — follow **this** CONTRACT and `fobject-ref` rule for the current repo.
- Do not confuse `UResource` (asset UObject) with `FRHIResource` (GPU object).

## Related files

- `Object.h`, pool / GC headers under `Core/`
- Journal: `Doc/Engine/DESIGN_JOURNAL.md` → Object / GC
