#pragma once

#include "Catty/Core/Export.h"
#include "Catty/Core/PoolAllocator.h"
#include "Catty/Resource/GCManager.h"
#include "Catty/Resource/Object.h"
#include "Catty/Resource/Package.h"
#include "Catty/Resource/Resource.h"
#include "Catty/Resource/ResourceHandle.h"
#include "Catty/Resource/ResourceServer.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Catty
{

/**
 * Package / FResource facade. Owns FResourceServer, FPackage & FResource pools,
 * and type-specific teardown. Both FPackage and FResource are FObject subclasses
 * registered with FGCManager (Root / RefCount).
 *
 * UnloadPackage drops the catalog Ref only — package/object Free is GC-driven.
 * Each in-package FObject holds Outer as FObjectRef so the package stays valid.
 */
class CATTY_API FResourceManager
{
public:
	static constexpr const char* TransientPackageName = "/Engine/Transient";

	FResourceManager() = default;
	~FResourceManager();

	FResourceManager(const FResourceManager&) = delete;
	FResourceManager& operator=(const FResourceManager&) = delete;

	/** Starts owned FResourceServer, then registers destroy handler on InGC. */
	[[nodiscard]] bool Initialize(FGCManager& InGC);
	void Shutdown();

	[[nodiscard]] bool IsInitialized() const { return GC != nullptr && Server.IsInitialized(); }

	[[nodiscard]] static std::string NormalizePackageName(std::string Name);

	[[nodiscard]] FObjectRef CreatePackage(
		std::string Name,
		EPackageFlags Flags = EPackageFlags::Transient);

	[[nodiscard]] FObjectRef GetTransientPackage();
	[[nodiscard]] FObjectRef FindPackage(const std::string& Name) const;

	/**
	 * Remove from catalog and Release catalog Ref.
	 * Does not destroy in-package objects; GC frees them when their RefCounts hit 0.
	 * Package memory frees when its RefCount hits 0 (no catalog, no object pins, no FObjectRef).
	 */
	bool UnloadPackage(const std::string& Name);

	[[nodiscard]] FObjectRef CreateResource(
		const FObjectRef& Package,
		std::string ObjectName,
		std::string SourcePath,
		EResourceType Type = EResourceType::Unknown);

	[[nodiscard]] FObjectRef CreateResource(
		std::string SourcePath,
		EResourceType Type = EResourceType::Unknown,
		std::string ObjectName = {});

	[[nodiscard]] FObjectRef FindObject(const FObjectRef& Package, const std::string& ObjectName) const;
	[[nodiscard]] FObjectRef FindObject(const std::string& PackageName, const std::string& ObjectName) const;

	[[nodiscard]] EResourceLoadState GetLoadState(FResourceId Id) const;
	[[nodiscard]] bool IsReady(FResourceId Id) const;

	[[nodiscard]] bool SavePackage(
		const FObjectRef& Package,
		const std::string& FilePath = {},
		bool bPretty = true,
		bool bSaveDependencies = true);
	[[nodiscard]] FObjectRef LoadPackage(const std::string& FilePath);

	/** Wait for raw fill if Object casts to FResource. */
	void Flush(const FObjectRef& Object);
	void FlushAll();

	void CollectGarbage();
	void TickGarbageCollection(float DeltaSeconds);

	[[nodiscard]] FResourceServer* GetServer() { return IsInitialized() ? &Server : nullptr; }
	[[nodiscard]] const FResourceServer* GetServer() const { return IsInitialized() ? &Server : nullptr; }

	[[nodiscard]] FGCManager* GetGC() { return GC; }
	[[nodiscard]] const FGCManager* GetGC() const { return GC; }

private:
	friend class FPackage;

	[[nodiscard]] static std::string NormalizeSourcePath(std::string Path);
	[[nodiscard]] static std::string MakeObjectNameFromSource(const std::string& SourcePath);
	[[nodiscard]] static EResourceType InferTypeFromPath(const std::string& Path);
	[[nodiscard]] static EResourceType ResourceTypeFromString(const std::string& Name);

	/** GC destroy-handler: claim FResource or FPackage. */
	[[nodiscard]] bool TryDestroyManagedObject(FObject* Object);

	void DestroyResource(FResource* Resource);
	void DestroyPackage(FPackage* Package);

	/** Drop catalog + Release catalog Ref (GC path). */
	void DropPackageFromCatalog(FPackage* Package);

	/**
	 * Shutdown helper: destroy zero-ref objects still listed; leave live-Ref objects.
	 * @return true if Objects map emptied.
	 */
	[[nodiscard]] bool DestroyPackageObjects(FPackage& Package, bool bForce);

	void FreePackageMemory(FPackage* Package);

	[[nodiscard]] bool SavePackageInternal(
		const FObjectRef& Package,
		const std::string& FilePath,
		bool bPretty,
		bool bSaveDependencies,
		std::unordered_set<std::string>& SavingPackageNames);

	[[nodiscard]] FObjectRef LoadPackageInternal(
		const std::string& FilePath,
		std::unordered_set<std::string>& LoadingFilePaths);

	[[nodiscard]] FObjectRef ResolveObjectPath(const std::string& PathName) const;

	FResourceServer Server;
	FGCManager* GC = nullptr;
	FGCManager::FObjectDestroyHandlerId DestroyHandlerId = FGCManager::InvalidDestroyHandlerId;
	/** Catalog: value holds the catalog FObjectRef (keeps package alive while loaded). */
	std::unordered_map<std::string, FObjectRef> Packages;
	FPackage* TransientPackage = nullptr;

	TPoolAllocator<FPackage> PackagePool{16};
	TPoolAllocator<FResource> ResourcePool{64};
};

} // namespace Catty
