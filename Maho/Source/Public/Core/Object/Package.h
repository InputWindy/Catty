#pragma once

#include <Core/Export.h>
#include <Core/Json.h>
#include <Core/Object/Object.h>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace Maho
{

class FResourceSystem;

/**
 * Package residency / save policy (UE PKG_* lite).
 * Transient: runtime-only; SavePackage refuses.
 * Persistent: may be written / loaded as JSON by the private Resource module.
 * Lifetime is RefCount only (catalog FObjectRef + Outer pins) — flags do not gate GC.
 */
MAHO_ENUM()
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
 * UE UPackage-lite: UObject subclass, pool-allocated, lifetime via FObjectRef + GC.
 * Not owned by the Resource module — kept alive by Outer FObjectRefs from packaged objects.
 * Objects map stores raw UObject* for name lookup only (non-owning).
 *
 * Example:
 * ```
 *   Maho::FGCSystem* GC = Maho::Detail::GetGCSystem();
 *   Maho::FObjectRef PkgRef = GC->NewObject<Maho::UPackage>(
 *       "/Game/Maps/Demo", Maho::EPackageFlags::Persistent);
 * ```
 */
MAHO_OBJECT()
class MAHO_API UPackage : public UObject
{
	MAHO_GENERATED_BODY()

public:
	/** Initial FGCSystem pool chunk slots (codegen RegisterGeneratedGCPooledTypes). */
	static constexpr int PoolSize = 16;

	UPackage(std::string InName, EPackageFlags InFlags = EPackageFlags::Transient);
	virtual ~UPackage() override;

	UPackage(const UPackage&) = delete;
	UPackage& operator=(const UPackage&) = delete;

	void OnPoolTearDown() override;

	// ---------------------------------------------------------------------------
	// Lookup / reflection — MAHO_FUNCTION (game / editor / Lua)
	// ---------------------------------------------------------------------------
	MAHO_FUNCTION()
	[[nodiscard]] FObjectRef FindObject(const std::string& InObjectName) const;
	MAHO_FUNCTION()
	[[nodiscard]] const std::string& GetFilePath() const { return FilePath; }
	MAHO_FUNCTION()
	void SetFilePath(std::string InPath) { FilePath = std::move(InPath); }
	MAHO_FUNCTION()
	[[nodiscard]] EPackageFlags GetPackageFlags() const { return PackageFlags; }
	MAHO_FUNCTION()
	[[nodiscard]] bool IsPersistent() const;
	MAHO_FUNCTION()
	[[nodiscard]] bool IsTransient() const;
	MAHO_FUNCTION()
	[[nodiscard]] std::uint32_t GetObjectCount() const
	{
		return static_cast<std::uint32_t>(Objects.size());
	}

private:
	friend class FResourceSystem;
	friend class UObject;

	// ---------------------------------------------------------------------------
	// Flags / file path (Resource module persistence)
	// ---------------------------------------------------------------------------
	void AddPackageFlags(EPackageFlags InFlags) { PackageFlags |= InFlags; }
	void ClearPackageFlags(EPackageFlags InFlags) { PackageFlags &= ~InFlags; }

	// ---------------------------------------------------------------------------
	// Object table
	// ---------------------------------------------------------------------------
	[[nodiscard]] bool RegisterObject(UObject* Object);
	/** Name-table only; called from UObject::ClearOuter. */
	void DetachObject(UObject* Object);

	// ---------------------------------------------------------------------------
	// Persistence IO (Resource module)
	// ---------------------------------------------------------------------------
	[[nodiscard]] bool Serialize(FJsonValue& OutObject) const;
	[[nodiscard]] bool Deserialize(const FJsonValue& InObject);

	// ---------------------------------------------------------------------------
	// Fields
	// ---------------------------------------------------------------------------
	std::string FilePath;
	EPackageFlags PackageFlags = EPackageFlags::Transient;
	/** Non-owning name table; lifetime owned by Refs + GC. */
	std::unordered_map<std::string, UObject*> Objects;
};

} // namespace Maho
