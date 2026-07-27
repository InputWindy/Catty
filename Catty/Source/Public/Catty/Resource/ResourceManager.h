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
#include <vector>

namespace Catty
{

/**
 * Package / FResource facade. Owns FResourceServer, FPackage & FResource pools,
 * and type-specific teardown (ResourceServer::Release). Registers objects with
 * FGCManager for Mark/Root/GC.
 *
 * FGCManager does not know FResource — destroy is claimed via AddObjectDestroyHandler.
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

	[[nodiscard]] FPackageRef CreatePackage(
		std::string Name,
		EPackageFlags Flags = EPackageFlags::Transient);

	[[nodiscard]] FPackageRef GetTransientPackage();
	[[nodiscard]] FPackageRef FindPackage(const std::string& Name) const;

	bool UnloadPackage(const std::string& Name);

	[[nodiscard]] FObjectRef CreateResource(
		const FPackageRef& Package,
		std::string ObjectName,
		std::string SourcePath,
		EResourceType Type = EResourceType::Unknown);

	[[nodiscard]] FObjectRef CreateResource(
		std::string SourcePath,
		EResourceType Type = EResourceType::Unknown,
		std::string ObjectName = {});

	[[nodiscard]] FObjectRef FindObject(const FPackageRef& Package, const std::string& ObjectName) const;
	[[nodiscard]] FObjectRef FindObject(const std::string& PackageName, const std::string& ObjectName) const;

	[[nodiscard]] EResourceLoadState GetLoadState(FResourceId Id) const;
	[[nodiscard]] bool IsReady(FResourceId Id) const;

	[[nodiscard]] bool SavePackage(const FPackageRef& Package, const std::string& FilePath = {}, bool bPretty = true);
	[[nodiscard]] FPackageRef LoadPackage(const std::string& FilePath);

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

	[[nodiscard]] static std::string NormalizePackageName(std::string Name);
	[[nodiscard]] static std::string NormalizeSourcePath(std::string Path);
	[[nodiscard]] static std::string MakeObjectNameFromSource(const std::string& SourcePath);
	[[nodiscard]] static EResourceType InferTypeFromPath(const std::string& Path);
	[[nodiscard]] static EResourceType ResourceTypeFromString(const std::string& Name);

	/** GC destroy-handler entry: true if Obj is an FResource we own. */
	[[nodiscard]] bool TryDestroyResourceObject(FObject* Object);

	void DestroyResource(FResource* Resource);
	void DestroyPackageObjects(FPackage& Package);
	/** Drop catalog registration and ReleaseRef (may Free when count hits 0). */
	void UnregisterAndReleasePackage(FPackage* Package);
	/** Pool Free only — called from FPackage::OnRefCountZero. */
	void FreePackageMemory(FPackage* Package);

	FResourceServer Server;
	FGCManager* GC = nullptr;
	std::unordered_map<std::string, FPackage*> Packages;
	FPackage* TransientPackage = nullptr;

	TPoolAllocator<FPackage> PackagePool{16};
	TPoolAllocator<FResource> ResourcePool{64};
};

} // namespace Catty
