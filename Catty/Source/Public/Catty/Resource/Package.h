#pragma once

#include "Catty/Core/Export.h"
#include "Catty/Core/Json.h"
#include "Catty/Resource/Object.h"
#include "Catty/Resource/PackageRef.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Catty
{

class FResourceManager;

/**
 * Package residency / save policy (UE PKG_* lite).
 * Transient: runtime-only; SavePackage refuses; GC may drop unused objects.
 * Persistent: may be written / loaded as JSON by FResourceManager.
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
 * UE UPackage-lite: pool-allocated, refcounted via FPackageRef.
 * Object table is name → FObject* (non-owning); object memory is in Resource pools.
 *
 * Example:
 * ```
 *   Catty::FPackageRef Pkg = ResourceManager.CreatePackage(
 *       "/Game/Maps/Demo", Catty::EPackageFlags::Persistent);
 *   ResourceManager.CreateResource(Pkg, "T_Hero", "Textures/T_Hero.png");
 *   ResourceManager.SavePackage(Pkg, "Content/Maps/Demo.pkg.json");
 * ```
 */
class CATTY_API FPackage
{
public:
	FPackage(std::string InName, EPackageFlags InFlags = EPackageFlags::Transient);
	~FPackage();

	FPackage(const FPackage&) = delete;
	FPackage& operator=(const FPackage&) = delete;

	[[nodiscard]] const std::string& GetName() const { return Name; }
	[[nodiscard]] const std::string& GetFilePath() const { return FilePath; }
	[[nodiscard]] EPackageFlags GetFlags() const { return Flags; }
	[[nodiscard]] std::uint32_t GetRefCount() const { return RefCount; }

	void SetFlags(EPackageFlags InFlags) { Flags = InFlags; }
	void AddFlags(EPackageFlags InFlags) { Flags |= InFlags; }
	void ClearFlags(EPackageFlags InFlags) { Flags &= ~InFlags; }

	[[nodiscard]] bool IsPersistent() const
	{
		return HasAnyPackageFlags(Flags, EPackageFlags::Persistent);
	}
	[[nodiscard]] bool IsTransient() const
	{
		return HasAnyPackageFlags(Flags, EPackageFlags::Transient) && !IsPersistent();
	}

	[[nodiscard]] FObjectRef FindObject(const std::string& ObjectName) const;

	[[nodiscard]] std::size_t GetObjectCount() const { return Objects.size(); }

	/** Snapshot of package objects as FObjectRef (each AddRef'd). */
	[[nodiscard]] std::vector<FObjectRef> GetObjects() const;

	[[nodiscard]] bool Serialize(FJsonValue& OutObject) const;
	[[nodiscard]] bool Deserialize(const FJsonValue& InObject);

	std::uint32_t AddRef();
	std::uint32_t ReleaseRef();

private:
	friend class FResourceManager;
	friend class FPackageRef;

	void SetFilePath(std::string InPath) { FilePath = std::move(InPath); }
	void SetOwner(FResourceManager* InOwner) { Owner = InOwner; }

	[[nodiscard]] bool RegisterObject(FObject* Object);
	[[nodiscard]] FObject* UnregisterObject(const std::string& ObjectName);
	void ClearObjects();

	/** Called when RefCount hits 0 — Free pool slot via Owner. */
	void OnRefCountZero();

	std::string Name;
	std::string FilePath;
	EPackageFlags Flags = EPackageFlags::Transient;
	std::unordered_map<std::string, FObject*> Objects;
	std::uint32_t RefCount = 0;
	FResourceManager* Owner = nullptr;
};

} // namespace Catty
