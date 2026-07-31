#pragma once

/**
 * Resource extension: UResource types, catalog, package IO, BulkData kick-off.
 * Type-specific work is IResourceImporter / IResourceExporter (private ResourceIO).
 */

#include <Core/DependsPack.h>
#include <Core/Export.h>
#include <Core/Extension/GC.h>
#include <Core/Object/Object.h>
#include <Core/Object/ObjectReflect.h>
#include <Core/Object/SoftObjectPath.h>
#include <Core/Sequencer/EngineExtension.h>
#include <Core/TypeList.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Catty
{

class UPackage;
class FGCSystem;
class FResourceSystem;
class FResourceServer;
class IResourceImporter;
class IResourceExporter;
void RegisterGeneratedResourceTypes(FResourceSystem& Manager, FGCSystem& GC);

CATTY_ENUM()
enum class EResourceLoadState : std::uint8_t
{
	Invalid = 0,
	Pending,
	Ready,
	Failed
};

/** High-level asset kind (extend as typed resources appear). */
CATTY_ENUM()
enum class EResourceType : std::uint8_t
{
	Unknown = 0,
	Raw,
	Texture,
	Mesh,
	Material,
	Shader,
	Audio,
	Data,
};

/**
 * External-file asset (png / mesh / ...).
 * Outer may be an UPackage (saved content) or null (runtime-only; PathName = ObjectName).
 * Created by the private Resource module; LoadState becomes Ready after BulkData import.
 */
CATTY_OBJECT()
class CATTY_API UResource : public UObject
{
	CATTY_GENERATED_BODY()

public:
	static constexpr int PoolSize = 64;

	UResource(
		UPackage* InOuter,
		std::string InObjectName,
		EResourceType InType,
		std::string InSourcePath);
	virtual ~UResource() override;

	void OnPoolTearDown() override;

	CATTY_FUNCTION()
	[[nodiscard]] EResourceType GetType() const { return Type; }
	CATTY_FUNCTION()
	[[nodiscard]] const std::string& GetSourcePath() const { return SourcePath; }
	/** Alias for GetSourcePath (legacy name). */
	CATTY_FUNCTION()
	[[nodiscard]] const std::string& GetPath() const { return SourcePath; }
	CATTY_FUNCTION()
	[[nodiscard]] EResourceLoadState GetLoadState() const { return LoadState; }

protected:
	friend class FResourceSystem;

	void SetLoadState(EResourceLoadState InState) { LoadState = InState; }

	CATTY_PROPERTY()
	EResourceType Type = EResourceType::Unknown;
	CATTY_PROPERTY()
	std::string SourcePath;
	CATTY_PROPERTY()
	EResourceLoadState LoadState = EResourceLoadState::Pending;
};

/**
 * Typed texture asset shell (decode / GPU upload live in private ResourceIO Traits).
 * Not CATTY_OBJECT — Lua FLua_* wrappers only support UObject as sol base today.
 */
class CATTY_API UTextureResource : public UResource
{
public:
	static constexpr int PoolSize = 32;

	UTextureResource(
		UPackage* InOuter,
		std::string InObjectName,
		EResourceType InType,
		std::string InSourcePath);
	virtual ~UTextureResource() override;
};

/** Raw bytes produced by FResourceServer; consumed by Importer. */
struct FResourceBulkData
{
	std::string SourcePath;
	std::vector<std::uint8_t> Bytes;
};

struct FResourceImportConfig
{
	FObjectRef Package;
	std::string ObjectName;
	std::string SourcePath;
	EResourceType TypeHint = EResourceType::Unknown;
};

struct FResourceExportConfig
{
	std::string DestinationPath;
	bool bOverwrite = true;
};

/**
 * Resource extension. Catalog / package / import services are public;
 * Initialize/Shutdown and IO registration stay private (codegen friend).
 */
class CATTY_API FResourceSystem final
	: public IEngineExtension
	, public TDependsPack<
		TDependsOn<EEngineStage::BeginFrame, TTypeList<FGCSystem>>,
		TDependsOn<EEngineStage::Init, TTypeList<FGCSystem>>>
{
public:
	FResourceSystem();
	~FResourceSystem() override;

	FResourceSystem(const FResourceSystem&) = delete;
	FResourceSystem& operator=(const FResourceSystem&) = delete;

	[[nodiscard]] bool RegisterResource(const FObjectRef& Resource);
	bool UnregisterResource(UObject* Resource);
	bool UnregisterResource(const FObjectRef& Resource);

	bool UnloadResource(const std::string& VirtualPath);
	bool UnloadResource(const FObjectRef& Resource);

	[[nodiscard]] FObjectRef TryLoad(const FSoftObjectPath& SoftPath);
	[[nodiscard]] FObjectRef TryLoad(const std::string& SoftPathString);

	[[nodiscard]] bool SavePackage(
		const FObjectRef& Package,
		const std::string& FilePath = {},
		bool bPretty = true,
		bool bSaveDependencies = true);
	[[nodiscard]] FObjectRef LoadPackage(const std::string& FilePath);

	[[nodiscard]] EResourceLoadState GetLoadState(const FObjectRef& Object) const;
	[[nodiscard]] bool IsReady(const FObjectRef& Object) const;
	void Flush(const FObjectRef& Object);
	void FlushAll();

	/** Resolve importer/exporter by config / live object — no type switch in Manager. */
	[[nodiscard]] FObjectRef KickImport(FResourceImportConfig Config);
	[[nodiscard]] bool KickExport(FResourceExportConfig Config, const FObjectRef& Resource);

	const char* GetName() const override { return "Resource"; }
	bool ExecuteStage(EEngineStage Stage) override;
	[[nodiscard]] bool IsIdle() const override;

private:
	friend void RegisterGeneratedResourceTypes(FResourceSystem& Manager, FGCSystem& GC);
	template <typename TResource>
	friend class TResourceImporter;
	friend class UResource;

	[[nodiscard]] bool Initialize();
	void Shutdown();
	[[nodiscard]] bool IsInitialized() const;
	void PrepareForExit();

	void RegisterImporter(std::unique_ptr<IResourceImporter> Importer);
	void RegisterExporter(std::unique_ptr<IResourceExporter> Exporter);
	void ClearImportersAndExporters();

	struct FPendingImport
	{
		std::uint64_t LoadId = 0;
		FObjectRef Resource;
		FResourceImportConfig Config;
		IResourceImporter* Importer = nullptr;
	};

	[[nodiscard]] static std::string NormalizePackageName(std::string Name);
	[[nodiscard]] static std::string NormalizeSourcePath(std::string Path);
	[[nodiscard]] static std::string MakeObjectNameFromSource(const std::string& SourcePath);
	[[nodiscard]] static std::string MakeResourceCatalogKey(const UResource& Resource);
	[[nodiscard]] static std::string NormalizeResourceVirtualPath(const std::string& VirtualPath);

	[[nodiscard]] IResourceImporter* FindImporter(const FResourceImportConfig& Config) const;
	[[nodiscard]] IResourceExporter* FindExporter(const FObjectRef& Resource) const;

	void UnregisterResourcesInPackage(const std::string& PackageName);
	void CancelPendingImport(UObject* Resource);
	void ProcessReadyImports();

	/** Create pooled TResource + catalog + kick BulkData; Importer retained for ApplyBulkData. */
	template <typename TResource>
	[[nodiscard]] FObjectRef BeginImport(
		FResourceImportConfig& Config,
		IResourceImporter* Importer);

	[[nodiscard]] bool HasActiveServer() const;
	[[nodiscard]] std::uint64_t RequestLoadId(const std::string& SourcePath);
	void ReleaseLoadId(std::uint64_t LoadId);
	[[nodiscard]] bool TakeBulkData(std::uint64_t LoadId, FResourceBulkData& OutBulk);

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
	std::unordered_map<std::uint64_t, FPendingImport> PendingImports;
	std::vector<std::unique_ptr<IResourceImporter>> Importers;
	std::vector<std::unique_ptr<IResourceExporter>> Exporters;
	bool bAcceptingNewWork = true;
};

namespace Detail
{
[[nodiscard]] FResourceSystem* GetResourceSystem();
}

} // namespace Catty
