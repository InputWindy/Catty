#pragma once

#include <Core/Export.h>
#include <Core/GC.h>
#include <Core/Object.h>
#include <Core/SoftObjectPath.h>
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
 * Registers FPackage / FResource pools on FGC and TearDown handlers.
 * Allocation goes through Catty::NewObject<T>() / FGC::NewObject; eviction drops
 * catalog FObjectRefs so GC can reclaim when live Refs hit 0.
 *
 * Catalogs hold FObjectRef for every loaded FPackage and FResource.
 * Resource keys are engine virtual paths (PackageName.ObjectName).
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
	[[nodiscard]] bool Initialize(FGC& InGC);
	void Shutdown();
	[[nodiscard]] bool IsInitialized() const;

	// --- Package ---
	[[nodiscard]] FObjectRef CreatePackage(
		std::string Name,
		EPackageFlags Flags = EPackageFlags::Transient);
	[[nodiscard]] FObjectRef GetTransientPackage();
	[[nodiscard]] FObjectRef FindPackage(const std::string& Name) const;
	/**
	 * Remove from package catalog and Release catalog Ref.
	 * Also drops catalog Refs for FResources in that package.
	 * Free is GC-driven when remaining Refs hit 0.
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

	/**
	 * Catalog lookup by engine virtual path (PackageName.ObjectName).
	 * Accepts SoftObjectPath strings (Class'...' stripped). Miss → empty Ref.
	 * Named FindResourceByPath — Windows headers #define FindResource → FindResourceA.
	 */
	[[nodiscard]] FObjectRef FindResourceByPath(const std::string& VirtualPath) const;

	/**
	 * Drop the resource's catalog FObjectRef (eviction). Does not Force-destroy;
	 * GC reclaims when no other live Refs remain.
	 */
	bool UnloadResource(const std::string& VirtualPath);
	bool UnloadResource(const FObjectRef& Resource);

	/**
	 * Soft path → live FObjectRef among already-loaded packages only.
	 * Does not LoadPackage. Invalid / missing / unloaded → empty Ref.
	 * Subobject paths (":Sub") are not walked yet — returns the package asset if present.
	 */
	[[nodiscard]] FObjectRef Resolve(const FSoftObjectPath& SoftPath) const;
	[[nodiscard]] FObjectRef Resolve(const std::string& SoftPathString) const;

	/**
	 * Resolve; if the package is not loaded, LoadPackage via FPaths then Resolve again.
	 * Caller-driven load (does not hide load cost inside Resolve).
	 */
	[[nodiscard]] FObjectRef TryLoad(const FSoftObjectPath& SoftPath);
	[[nodiscard]] FObjectRef TryLoad(const std::string& SoftPathString);

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

	// --- GC destroy prep (no pool Free — FGC::DestroyObjectImmediate Frees) ---
	void TearDownResource(FResource* Resource);
	void TearDownPackage(FPackage* Package);
	void DropPackageFromCatalog(FPackage* Package);
	void DropResourceFromCatalog(FResource* Resource);
	void DropPackageResourcesFromCatalog(FPackage& Package);
	[[nodiscard]] static std::string MakeResourceCatalogKey(const FResource& Resource);
	[[nodiscard]] static std::string NormalizeResourceVirtualPath(const std::string& VirtualPath);
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
	FGC* GC = nullptr;
	/** Catalog: value holds the catalog FObjectRef (keeps package alive while loaded). */
	std::unordered_map<std::string, FObjectRef> Packages;
	FPackage* TransientPackage = nullptr;
	/**
	 * Catalog of loaded FResources keyed by engine virtual path
	 * (PackageName.ObjectName, e.g. "/Game/Weapons/Sword.Sword").
	 */
	std::unordered_map<std::string, FObjectRef> Resources;
};

} // namespace Catty
