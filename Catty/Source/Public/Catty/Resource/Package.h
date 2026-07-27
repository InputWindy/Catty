#pragma once

#include "Catty/Core/Export.h"
#include "Catty/Core/Json.h"
#include "Catty/Resource/Object.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Catty
{

/**
 * Package residency / save policy (UE PKG_* lite).
 * Transient: runtime-only; SavePackage refuses; GC may drop unused exports.
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
 * UE UPackage-lite: owns export *registration* (name → FObject*).
 * Object memory lives in FResourceManager pools; FGCManager only tracks lifetime/GC.
 * Package JSON load/save is synchronous info only.
 *
 * Example:
 * ```
 *   Catty::FPackage* Pkg = ResourceManager.CreatePackage(
 *       "/Game/Maps/Demo", Catty::EPackageFlags::Persistent);
 *   ResourceManager.CreateResource(*Pkg, "T_Hero", "Textures/T_Hero.png");
 *   ResourceManager.SavePackage(*Pkg, "Content/Maps/Demo.pkg.json");
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

	[[nodiscard]] FObject* FindObject(const std::string& ObjectName) const;

	template <typename TObject>
	[[nodiscard]] TObject* FindObjectAs(const std::string& ObjectName) const
	{
		return dynamic_cast<TObject*>(FindObject(ObjectName));
	}

	[[nodiscard]] std::size_t GetExportCount() const { return Exports.size(); }

	/** Snapshot of export pointers (valid while package is loaded). */
	[[nodiscard]] std::vector<FObject*> GetExports() const;

	/**
	 * Write package metadata + export table (Resource: name/type/source).
	 * Called by FResourceManager::SavePackage.
	 */
	[[nodiscard]] bool Serialize(FJsonValue& OutObject) const;

	/**
	 * Read package metadata only. Exports are created by FResourceManager::LoadPackage.
	 */
	[[nodiscard]] bool Deserialize(const FJsonValue& InObject);

private:
	friend class FResourceManager;

	void SetFilePath(std::string InPath) { FilePath = std::move(InPath); }

	/** Registers a pool-allocated export (non-owning). ObjectName must be unique. Sets Outer. */
	[[nodiscard]] bool RegisterExport(FObject* Object);

	/**
	 * Unregisters export; caller (Manager) must return memory to the matching TPoolAllocator.
	 * @return pointer or nullptr.
	 */
	[[nodiscard]] FObject* UnregisterExport(const std::string& ObjectName);

	/** Clears the export map only (does not Free pool memory). Exports must already be empty or Manager will Free. */
	void ClearExports();

	std::string Name;
	std::string FilePath;
	EPackageFlags Flags = EPackageFlags::Transient;
	std::unordered_map<std::string, FObject*> Exports;
};

} // namespace Catty
