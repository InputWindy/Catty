#pragma once

#include "Catty/Core/Export.h"
#include "Catty/Core/Json.h"
#include "Catty/Resource/Object.h"

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
class CATTY_API FPackage : public FObject
{
public:
	FPackage(std::string InName, EPackageFlags InFlags = EPackageFlags::Transient);
	virtual ~FPackage() override;

	FPackage(const FPackage&) = delete;
	FPackage& operator=(const FPackage&) = delete;

	// --- Queries ---
	[[nodiscard]] const std::string& GetFilePath() const { return FilePath; }
	[[nodiscard]] bool IsPersistent() const;
	[[nodiscard]] bool IsTransient() const;
	[[nodiscard]] std::size_t GetObjectCount() const { return Objects.size(); }

	// --- Lookup ---
	[[nodiscard]] FObjectRef FindObject(const std::string& InObjectName) const;

private:
	friend class FResourceManager;
	friend class FObject;

	// --- Catalog / residency (FResourceManager) ---
	void SetFilePath(std::string InPath) { FilePath = std::move(InPath); }
	void AddPackageFlags(EPackageFlags InFlags) { PackageFlags |= InFlags; }
	void ClearPackageFlags(EPackageFlags InFlags) { PackageFlags &= ~InFlags; }

	// --- Object table ---
	[[nodiscard]] bool RegisterObject(FObject* Object);
	/** Name-table only; called from FObject::ClearOuter. */
	void DetachObject(FObject* Object);

	// --- Persistence IO (FResourceManager) ---
	[[nodiscard]] bool Serialize(FJsonValue& OutObject) const;
	[[nodiscard]] bool Deserialize(const FJsonValue& InObject);

	std::string FilePath;
	EPackageFlags PackageFlags = EPackageFlags::Transient;
	/** Non-owning name table; lifetime owned by Refs + GC. */
	std::unordered_map<std::string, FObject*> Objects;
};

} // namespace Catty
