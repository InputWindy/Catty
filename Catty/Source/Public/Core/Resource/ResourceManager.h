#pragma once

#include <Core/Export.h>
#include <Core/Object.h>
#include <Core/SoftObjectPath.h>
#include <Core/Resource/Package.h>
#include <Core/Resource/Resource.h>
#include <Core/Resource/ResourceHandle.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Catty
{

class FResourceServer;

/**
 * Resource catalog + package IO (built into Catty.dll).
 * Owns PathName → FObjectRef catalog only. Does not create game resources —
 * callers NewObject<FResource> then RegisterResource. LoadPackage rebuilds
 * objects internally while deserializing.
 *
 * Access via free helpers (same pattern as FGC / NewObject), not ILuaBindable.
 */
class CATTY_API FResourceManager
{
public:
	FResourceManager();
	~FResourceManager();

	FResourceManager(const FResourceManager&) = delete;
	FResourceManager& operator=(const FResourceManager&) = delete;

	// --- Lifecycle (module) ---
	[[nodiscard]] bool Initialize();
	void Shutdown();
	[[nodiscard]] bool IsInitialized() const;

	// --- Catalog ---
	[[nodiscard]] bool RegisterResource(const FObjectRef& Resource);
	bool UnregisterResource(FObject* Resource);
	bool UnregisterResource(const FObjectRef& Resource);

	// --- Query ---
	[[nodiscard]] FObjectRef FindObject(const FObjectRef& Package, const std::string& ObjectName) const;
	[[nodiscard]] FObjectRef FindObject(const std::string& PackageName, const std::string& ObjectName) const;
	/**
	 * Catalog lookup by PathName (PackageName.ObjectName).
	 * Named FindResourceByPath — Windows headers #define FindResource → FindResourceA.
	 */
	[[nodiscard]] FObjectRef FindResourceByPath(const std::string& VirtualPath) const;
	bool UnloadResource(const std::string& VirtualPath);
	bool UnloadResource(const FObjectRef& Resource);

	[[nodiscard]] FObjectRef Resolve(const FSoftObjectPath& SoftPath) const;
	[[nodiscard]] FObjectRef Resolve(const std::string& SoftPathString) const;
	[[nodiscard]] FObjectRef TryLoad(const FSoftObjectPath& SoftPath);
	[[nodiscard]] FObjectRef TryLoad(const std::string& SoftPathString);

	/** Release async load Id (FResource TearDown). */
	void ReleaseResourceId(FResourceId Id);

	// --- Load / export ---
	[[nodiscard]] bool SavePackage(
		const FObjectRef& Package,
		const std::string& FilePath = {},
		bool bPretty = true,
		bool bSaveDependencies = true);
	[[nodiscard]] FObjectRef LoadPackage(const std::string& FilePath);

	// --- Async raw bytes ---
	[[nodiscard]] EResourceLoadState GetLoadState(const FObjectRef& Object) const;
	[[nodiscard]] bool IsReady(const FObjectRef& Object) const;
	void Flush(const FObjectRef& Object);
	void FlushAll();

private:
	[[nodiscard]] static std::string NormalizePackageName(std::string Name);
	[[nodiscard]] static std::string NormalizeSourcePath(std::string Path);
	[[nodiscard]] static std::string MakeObjectNameFromSource(const std::string& SourcePath);
	[[nodiscard]] static EResourceType InferTypeFromPath(const std::string& Path);
	[[nodiscard]] static EResourceType ResourceTypeFromString(const std::string& Name);
	[[nodiscard]] static std::string MakeResourceCatalogKey(const FResource& Resource);
	[[nodiscard]] static std::string NormalizeResourceVirtualPath(const std::string& VirtualPath);

	[[nodiscard]] FObjectRef FindLoadedPackageByName(const std::string& Name) const;
	void UnregisterResourcesInPackage(const std::string& PackageName);

	/** Deserialize path only: NewObject + package table + catalog Register. */
	[[nodiscard]] FObjectRef LoadResourceIntoPackage(
		const FObjectRef& Package,
		std::string ObjectName,
		std::string SourcePath,
		EResourceType Type);

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
	std::unordered_map<std::string, FObjectRef> Resources;
};

// ---------------------------------------------------------------------------
// Public free helpers (resolve via GApp Resource module), same style as FGC.
// ---------------------------------------------------------------------------

namespace Detail
{
[[nodiscard]] CATTY_API FResourceManager* GetResourceManager();
}

inline bool RegisterResource(const FObjectRef& Resource)
{
	FResourceManager* Manager = Detail::GetResourceManager();
	return Manager && Manager->RegisterResource(Resource);
}

inline bool UnregisterResource(FObject* Resource)
{
	FResourceManager* Manager = Detail::GetResourceManager();
	return Manager && Manager->UnregisterResource(Resource);
}

inline bool UnregisterResource(const FObjectRef& Resource)
{
	FResourceManager* Manager = Detail::GetResourceManager();
	return Manager && Manager->UnregisterResource(Resource);
}

[[nodiscard]] inline FObjectRef FindObject(const FObjectRef& Package, const std::string& ObjectName)
{
	FResourceManager* Manager = Detail::GetResourceManager();
	return Manager ? Manager->FindObject(Package, ObjectName) : FObjectRef{};
}

[[nodiscard]] inline FObjectRef FindObject(const std::string& PackageName, const std::string& ObjectName)
{
	FResourceManager* Manager = Detail::GetResourceManager();
	return Manager ? Manager->FindObject(PackageName, ObjectName) : FObjectRef{};
}

[[nodiscard]] inline FObjectRef FindResourceByPath(const std::string& VirtualPath)
{
	FResourceManager* Manager = Detail::GetResourceManager();
	return Manager ? Manager->FindResourceByPath(VirtualPath) : FObjectRef{};
}

inline bool UnloadResource(const std::string& VirtualPath)
{
	FResourceManager* Manager = Detail::GetResourceManager();
	return Manager && Manager->UnloadResource(VirtualPath);
}

inline bool UnloadResource(const FObjectRef& Resource)
{
	FResourceManager* Manager = Detail::GetResourceManager();
	return Manager && Manager->UnloadResource(Resource);
}

[[nodiscard]] inline FObjectRef Resolve(const FSoftObjectPath& SoftPath)
{
	FResourceManager* Manager = Detail::GetResourceManager();
	return Manager ? Manager->Resolve(SoftPath) : FObjectRef{};
}

[[nodiscard]] inline FObjectRef Resolve(const std::string& SoftPathString)
{
	FResourceManager* Manager = Detail::GetResourceManager();
	return Manager ? Manager->Resolve(SoftPathString) : FObjectRef{};
}

[[nodiscard]] inline FObjectRef TryLoad(const FSoftObjectPath& SoftPath)
{
	FResourceManager* Manager = Detail::GetResourceManager();
	return Manager ? Manager->TryLoad(SoftPath) : FObjectRef{};
}

[[nodiscard]] inline FObjectRef TryLoad(const std::string& SoftPathString)
{
	FResourceManager* Manager = Detail::GetResourceManager();
	return Manager ? Manager->TryLoad(SoftPathString) : FObjectRef{};
}

inline void ReleaseResourceId(FResourceId Id)
{
	if (FResourceManager* Manager = Detail::GetResourceManager())
	{
		Manager->ReleaseResourceId(Id);
	}
}

[[nodiscard]] inline bool SavePackage(
	const FObjectRef& Package,
	const std::string& FilePath = {},
	bool bPretty = true,
	bool bSaveDependencies = true)
{
	FResourceManager* Manager = Detail::GetResourceManager();
	return Manager && Manager->SavePackage(Package, FilePath, bPretty, bSaveDependencies);
}

[[nodiscard]] inline FObjectRef LoadPackage(const std::string& FilePath)
{
	FResourceManager* Manager = Detail::GetResourceManager();
	return Manager ? Manager->LoadPackage(FilePath) : FObjectRef{};
}

[[nodiscard]] inline EResourceLoadState GetResourceLoadState(const FObjectRef& Object)
{
	FResourceManager* Manager = Detail::GetResourceManager();
	return Manager ? Manager->GetLoadState(Object) : EResourceLoadState::Invalid;
}

[[nodiscard]] inline bool IsResourceReady(const FObjectRef& Object)
{
	FResourceManager* Manager = Detail::GetResourceManager();
	return Manager && Manager->IsReady(Object);
}

inline void FlushResource(const FObjectRef& Object)
{
	if (FResourceManager* Manager = Detail::GetResourceManager())
	{
		Manager->Flush(Object);
	}
}

inline void FlushAllResources()
{
	if (FResourceManager* Manager = Detail::GetResourceManager())
	{
		Manager->FlushAll();
	}
}

} // namespace Catty
