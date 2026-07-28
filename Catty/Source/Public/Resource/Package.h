#pragma once

#include "Core/Export.h"
#include "Core/Json.h"
#include "Resource/Object.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace Catty
{

class FResourceManager;

/**
 * Package residency / save policy (UE PKG_* lite).
 * Transient: runtime-only; SavePackage refuses.
 * Persistent: may be written / loaded as JSON by FResourceManager.
 * Lifetime is RefCount only (catalog FObjectRef + Outer pins) — flags do not gate GC.
 */
CATTY_ENUM()
enum class EPackageFlags : std::uint32_t
{
	None = 0,
	Transient = 1u << 0,
	Persistent = 1u << 1,
};

[[nodiscard]] constexpr EPackageFlags operator|(EPackageFlags A, EPackageFlags B)
{
	return static_cast<EPackageFlags>(static_cast<std::uint32_t>(A) | static_cast<std::uint32_t>(B));
}

[[nodiscard]] constexpr EPackageFlags operator&(EPackageFlags A, EPackageFlags B)
{
	return static_cast<EPackageFlags>(static_cast<std::uint32_t>(A) & static_cast<std::uint32_t>(B));
}

[[nodiscard]] constexpr EPackageFlags operator~(EPackageFlags A)
{
	return static_cast<EPackageFlags>(~static_cast<std::uint32_t>(A));
}

inline EPackageFlags& operator|=(EPackageFlags& A, EPackageFlags B)
{
	A = A | B;
	return A;
}

inline EPackageFlags& operator&=(EPackageFlags& A, EPackageFlags B)
{
	A = A & B;
	return A;
}

[[nodiscard]] constexpr bool HasAnyPackageFlags(EPackageFlags Value, EPackageFlags Test)
{
	return (static_cast<std::uint32_t>(Value) & static_cast<std::uint32_t>(Test)) != 0;
}

/**
 * UE UPackage-lite: FObject subclass, pool-allocated, lifetime via FObjectRef + GC.
 * Catalog map holds one FObjectRef while loaded; each in-package FObject
 * holds Outer as FObjectRef so the package stays alive until the last object dies.
 *
 * UnloadPackage only drops the catalog Ref — Free happens when RefCount hits 0 (GC).
 * Objects map stores raw FObject* for name lookup only (non-owning).
 *
 * Example:
 * ```
 *   Catty::FObjectRef Pkg = ResourceManager.CreatePackage(
 *       "/Game/Maps/Demo", Catty::EPackageFlags::Persistent);
 *   ResourceManager.CreateResource(Pkg, "T_Hero", "Textures/T_Hero.png");
 *   ResourceManager.SavePackage(Pkg, "Content/Maps/Demo.pkg.json");
 * ```
 */
CATTY_OBJECT()
class CATTY_API FPackage : public FObject
{
	CATTY_GENERATED_BODY()

public:
	FPackage(std::string InName, EPackageFlags InFlags = EPackageFlags::Transient);
	virtual ~FPackage() override;

	FPackage(const FPackage&) = delete;
	FPackage& operator=(const FPackage&) = delete;

	// ---------------------------------------------------------------------------
	// Lookup / reflection — CATTY_FUNCTION (game / editor / Lua)
	// ---------------------------------------------------------------------------
	CATTY_FUNCTION()
	[[nodiscard]] FObjectRef FindObject(const std::string& InObjectName) const;
	CATTY_FUNCTION()
	[[nodiscard]] const std::string& GetFilePath() const { return FilePath; }
	CATTY_FUNCTION()
	void SetFilePath(std::string InPath) { FilePath = std::move(InPath); }
	CATTY_FUNCTION()
	[[nodiscard]] EPackageFlags GetPackageFlags() const { return PackageFlags; }
	CATTY_FUNCTION()
	[[nodiscard]] bool IsPersistent() const;
	CATTY_FUNCTION()
	[[nodiscard]] bool IsTransient() const;
	CATTY_FUNCTION()
	[[nodiscard]] std::uint32_t GetObjectCount() const
	{
		return static_cast<std::uint32_t>(Objects.size());
	}

private:
	friend class FResourceManager;
	friend class FObject;

	// ---------------------------------------------------------------------------
	// Catalog / residency (FResourceManager)
	// ---------------------------------------------------------------------------
	void AddPackageFlags(EPackageFlags InFlags) { PackageFlags |= InFlags; }
	void ClearPackageFlags(EPackageFlags InFlags) { PackageFlags &= ~InFlags; }

	// ---------------------------------------------------------------------------
	// Object table
	// ---------------------------------------------------------------------------
	[[nodiscard]] bool RegisterObject(FObject* Object);
	/** Name-table only; called from FObject::ClearOuter. */
	void DetachObject(FObject* Object);

	// ---------------------------------------------------------------------------
	// Persistence IO (FResourceManager)
	// ---------------------------------------------------------------------------
	[[nodiscard]] bool Serialize(FJsonValue& OutObject) const;
	[[nodiscard]] bool Deserialize(const FJsonValue& InObject);

	// ---------------------------------------------------------------------------
	// Fields
	// ---------------------------------------------------------------------------
	std::string FilePath;
	EPackageFlags PackageFlags = EPackageFlags::Transient;
	/** Non-owning name table; lifetime owned by Refs + GC. */
	std::unordered_map<std::string, FObject*> Objects;
};

} // namespace Catty
