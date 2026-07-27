#pragma once

#include "Catty/Core/Export.h"
#include "Catty/Resource/Object.h"
#include "Catty/Resource/Package.h"
#include "Catty/Resource/Resource.h"
#include "Catty/Resource/ResourceHandle.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Catty
{

class FResourceServer;

/**
 * Game-thread facade: Package-centric (UE LoadPackage / SavePackage / NewObject lite).
 * Every FResource / FObject is uniquely Outer'd to one FPackage.
 *
 * Sync vs async:
 *   - LoadPackage / SavePackage / CreatePackage are synchronous (JSON info only).
 *   - CreateResource / LoadPackage create FResource objects immediately on the game thread;
 *     ResourceServer fills raw payload asynchronously afterward.
 *   - Use Flush(Resource) to wait until that resource's raw data is Ready/Failed.
 *
 * Example:
 * ```
 *   Catty::FPackage* Pkg = ResourceManager.CreatePackage(
 *       "/Game/Maps/Demo", Catty::EPackageFlags::Persistent);
 *   Catty::FResourceRef Hero = ResourceManager.CreateResource(
 *       *Pkg, "T_Hero", "Textures/T_Hero.png");
 *   ResourceManager.SavePackage(*Pkg, "Content/Maps/Demo.pkg.json");
 *
 *   Catty::FPackage* Loaded = ResourceManager.LoadPackage("Content/Maps/Demo.pkg.json");
 *   // Package + export objects exist now; raw may still be Pending:
 *   Catty::FResource* Tex = ResourceManager.FindObjectAs<Catty::FResource>(*Loaded, "T_Hero");
 *   ResourceManager.Flush(*Tex);
 * ```
 */
class CATTY_API FResourceManager
{
public:
	static constexpr const char* TransientPackageName = "/Engine/Transient";

	FResourceManager() = default;
	~FResourceManager();

	FResourceManager(const FResourceManager&) = delete;
	FResourceManager& operator=(const FResourceManager&) = delete;

	[[nodiscard]] bool Initialize(FResourceServer& InServer);
	void Shutdown();

	[[nodiscard]] bool IsInitialized() const { return Server != nullptr; }

	/** Create an empty package (fails if name already exists). */
	[[nodiscard]] FPackage* CreatePackage(
		std::string Name,
		EPackageFlags Flags = EPackageFlags::Transient);

	/** UE GetTransientPackage — creates /Engine/Transient on first use. */
	[[nodiscard]] FPackage* GetTransientPackage();

	[[nodiscard]] FPackage* FindPackage(const std::string& Name) const;

	/**
	 * Destroy package and all exports (releases ResourceServer ids for FResource).
	 * Cannot unload the transient package while manager is alive (cleared on Shutdown).
	 */
	bool UnloadPackage(const std::string& Name);

	/**
	 * Create an FResource export inside Package immediately (ObjectName unique within package).
	 * Object exists on return; ResourceServer::RequestLoad fills raw data asynchronously.
	 */
	[[nodiscard]] FResourceRef CreateResource(
		FPackage& Package,
		std::string ObjectName,
		std::string SourcePath,
		EResourceType Type = EResourceType::Unknown);

	/**
	 * Convenience: CreateResource into TransientPackage.
	 * ObjectName defaults to file stem of SourcePath when empty.
	 */
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

	/**
	 * Synchronously write package JSON (name / flags / exports — not raw bytes).
	 * Uses Package.GetFilePath() when FilePath is empty.
	 */
	[[nodiscard]] bool SavePackage(FPackage& Package, const std::string& FilePath = {}, bool bPretty = true);

	/**
	 * Synchronously load package JSON and create all export objects on this thread.
	 * Each FResource is registered immediately; raw fill stays async on ResourceServer.
	 * Returns nullptr only if the JSON cannot be read/parsed (not because raw is pending).
	 */
	[[nodiscard]] FPackage* LoadPackage(const std::string& FilePath);

	/** Wait until this resource's raw payload is Ready/Failed (not a Package operation). */
	void Flush(const FResource& Resource);

	/** Flush(*Ref) when valid; no-op if empty. */
	void Flush(const FResourceRef& Resource);

	/** Drain the ResourceServer worker queue (all pending raw loads). */
	void FlushAll();

	/**
	 * Transient packages: destroy exports with RefCount==0 (and Release server).
	 * Persistent package exports stay until UnloadPackage.
	 */
	void CollectGarbage();

	void TickGarbageCollection(float DeltaSeconds);

	void SetGarbageCollectionInterval(float Seconds) { GcIntervalSeconds = Seconds; }
	[[nodiscard]] float GetGarbageCollectionInterval() const { return GcIntervalSeconds; }

	[[nodiscard]] FResourceServer* GetServer() { return Server; }
	[[nodiscard]] const FResourceServer* GetServer() const { return Server; }

private:
	[[nodiscard]] static std::string NormalizePackageName(std::string Name);
	[[nodiscard]] static std::string NormalizeSourcePath(std::string Path);
	[[nodiscard]] static std::string MakeObjectNameFromSource(const std::string& SourcePath);
	[[nodiscard]] static EResourceType InferTypeFromPath(const std::string& Path);
	[[nodiscard]] static EResourceType ResourceTypeFromString(const std::string& Name);

	void DestroyResourceExport(FResource& Resource);
	void DestroyPackageExports(FPackage& Package);

	FResourceServer* Server = nullptr;
	std::unordered_map<std::string, std::unique_ptr<FPackage>> Packages;
	FPackage* TransientPackage = nullptr;

	float GcIntervalSeconds = 1.0f;
	float GcAccumulatorSeconds = 0.0f;
};

} // namespace Catty
