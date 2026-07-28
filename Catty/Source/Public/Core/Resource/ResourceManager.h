#pragma once

#include <Core/Export.h>
#include <Core/PoolAllocator.h>
#include <Core/GCManager.h>
#include <Core/Object.h>
#include <Core/Resource/Package.h>
#include <Core/Resource/Resource.h>
#include <Core/Resource/ResourceHandle.h>
#include <Core/Layer/ScriptSystem.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Catty
{

class FResourceServer;

/**
 * Package / FResource facade (built into Catty.dll).
 * Owns FResourceServer (private impl), FPackage & FResource pools,
 * and type-specific teardown. Both FPackage and FResource are FObject subclasses
 * registered with FGCManager (Root / RefCount).
 *
 * UnloadPackage drops the catalog Ref only — package/object Free is GC-driven.
 * Each in-package FObject holds Outer as FObjectRef so the package stays valid.
 * GC ticks via FGCManager (FApp), not through this manager.
 *
 * Lua: implements ILuaBindable; BindLua registers catalog / create / load helpers on `catty.*`.
 */
class CATTY_API FResourceManager : public ILuaBindable
{
public:
	static constexpr const char* TransientPackageName = "/Engine/Transient";

	FResourceManager();
	~FResourceManager() override;

	FResourceManager(const FResourceManager&) = delete;
	FResourceManager& operator=(const FResourceManager&) = delete;

	/** Register ResourceManager APIs into Script's `catty` table. */
	void BindLua(FScriptSystem& Script) override;

	// --- Lifecycle ---
	[[nodiscard]] bool Initialize(FGCManager& InGC);
	void Shutdown();
	[[nodiscard]] bool IsInitialized() const;

	// --- Package ---
	[[nodiscard]] FObjectRef CreatePackage(
		std::string Name,
		EPackageFlags Flags = EPackageFlags::Transient);
	[[nodiscard]] FObjectRef GetTransientPackage();
	[[nodiscard]] FObjectRef FindPackage(const std::string& Name) const;
	/**
	 * Remove from catalog and Release catalog Ref.
	 * Does not destroy in-package objects; GC frees them when RefCounts hit 0.
	 */
	bool UnloadPackage(const std::string& Name);

	// --- Object / Resource ---
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

	// --- Persistence ---
	[[nodiscard]] bool SavePackage(
		const FObjectRef& Package,
		const std::string& FilePath = {},
		bool bPretty = true,
		bool bSaveDependencies = true);
	[[nodiscard]] FObjectRef LoadPackage(const std::string& FilePath);

	// --- Async raw load ---
	[[nodiscard]] EResourceLoadState GetLoadState(const FObjectRef& Object) const;
	[[nodiscard]] bool IsReady(const FObjectRef& Object) const;
	void Flush(const FObjectRef& Object);
	void FlushAll();

private:
	// --- Path / type helpers ---
	[[nodiscard]] static std::string NormalizePackageName(std::string Name);
	[[nodiscard]] static std::string NormalizeSourcePath(std::string Path);
	[[nodiscard]] static std::string MakeObjectNameFromSource(const std::string& SourcePath);
	[[nodiscard]] static EResourceType InferTypeFromPath(const std::string& Path);
	[[nodiscard]] static EResourceType ResourceTypeFromString(const std::string& Name);

	// --- GC destroy handler / pool teardown ---
	[[nodiscard]] bool TryDestroyManagedObject(FObject* Object);
	void DestroyResource(FResource* Resource);
	void DestroyPackage(FPackage* Package);
	void DropPackageFromCatalog(FPackage* Package);
	[[nodiscard]] bool DestroyPackageObjects(FPackage& Package, bool bForce);

	// --- Save / Load internals ---
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

	std::unique_ptr<FResourceServer> Server;
	FGCManager* GC = nullptr;
	FGCManager::FObjectDestroyHandlerId DestroyHandlerId = FGCManager::InvalidDestroyHandlerId;
	/** Catalog: value holds the catalog FObjectRef (keeps package alive while loaded). */
	std::unordered_map<std::string, FObjectRef> Packages;
	FPackage* TransientPackage = nullptr;

	TPoolAllocator<FPackage> PackagePool{16};
	TPoolAllocator<FResource> ResourcePool{64};
};

} // namespace Catty
