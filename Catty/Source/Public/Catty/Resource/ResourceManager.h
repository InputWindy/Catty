#pragma once

#include "Catty/Core/Export.h"
#include "Catty/Core/PoolAllocator.h"
#include "Catty/Resource/GCManager.h"
#include "Catty/Resource/Object.h"
#include "Catty/Resource/Package.h"
#include "Catty/Resource/Resource.h"
#include "Catty/Resource/ResourceHandle.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Catty
{

class FResourceServer;

/**
 * Package / FResource facade. Owns FPackage & FResource pools and type-specific
 * teardown (ResourceServer::Release). Registers objects with FGCManager for Mark/Root/GC.
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

	[[nodiscard]] bool Initialize(FResourceServer& InServer, FGCManager& InGC);
	void Shutdown();

	[[nodiscard]] bool IsInitialized() const { return Server != nullptr && GC != nullptr; }

	[[nodiscard]] FPackage* CreatePackage(
		std::string Name,
		EPackageFlags Flags = EPackageFlags::Transient);

	[[nodiscard]] FPackage* GetTransientPackage();
	[[nodiscard]] FPackage* FindPackage(const std::string& Name) const;

	bool UnloadPackage(const std::string& Name);

	[[nodiscard]] FResourceRef CreateResource(
		FPackage& Package,
		std::string ObjectName,
		std::string SourcePath,
		EResourceType Type = EResourceType::Unknown);

	[[nodiscard]] FResourceRef CreateResource(
		std::string SourcePath,
		EResourceType Type = EResourceType::Unknown,
		std::string ObjectName = {});

	[[nodiscard]] FObject* FindObject(FPackage& Package, const std::string& ObjectName) const;
	[[nodiscard]] FObject* FindObject(const std::string& PackageName, const std::string& ObjectName) const;

	template <typename TObject>
	[[nodiscard]] TObject* FindObjectAs(FPackage& Package, const std::string& ObjectName) const
	{
		return dynamic_cast<TObject*>(FindObject(Package, ObjectName));
	}

	template <typename TObject>
	[[nodiscard]] TObject* FindObjectAs(const std::string& PackageName, const std::string& ObjectName) const
	{
		return dynamic_cast<TObject*>(FindObject(PackageName, ObjectName));
	}

	[[nodiscard]] EResourceLoadState GetLoadState(FResourceId Id) const;
	[[nodiscard]] bool IsReady(FResourceId Id) const;

	[[nodiscard]] bool SavePackage(FPackage& Package, const std::string& FilePath = {}, bool bPretty = true);
	[[nodiscard]] FPackage* LoadPackage(const std::string& FilePath);

	void Flush(const FResource& Resource);
	void Flush(const FResourceRef& Resource);
	void FlushAll();

	void CollectGarbage();
	void TickGarbageCollection(float DeltaSeconds);

	[[nodiscard]] FResourceServer* GetServer() { return Server; }
	[[nodiscard]] const FResourceServer* GetServer() const { return Server; }

	[[nodiscard]] FGCManager* GetGC() { return GC; }
	[[nodiscard]] const FGCManager* GetGC() const { return GC; }

private:
	[[nodiscard]] static std::string NormalizePackageName(std::string Name);
	[[nodiscard]] static std::string NormalizeSourcePath(std::string Path);
	[[nodiscard]] static std::string MakeObjectNameFromSource(const std::string& SourcePath);
	[[nodiscard]] static EResourceType InferTypeFromPath(const std::string& Path);
	[[nodiscard]] static EResourceType ResourceTypeFromString(const std::string& Name);

	/** GC destroy-handler entry: true if Obj is an FResource we own. */
	[[nodiscard]] bool TryDestroyResourceObject(FObject* Object);

	void DestroyResource(FResource* Resource);
	void DestroyPackageExports(FPackage& Package);
	void DestroyPackage(FPackage* Package);

	FResourceServer* Server = nullptr;
	FGCManager* GC = nullptr;
	std::unordered_map<std::string, FPackage*> Packages;
	FPackage* TransientPackage = nullptr;

	TPoolAllocator<FPackage> PackagePool{16};
	TPoolAllocator<FResource> ResourcePool{64};
};

} // namespace Catty
