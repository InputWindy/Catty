#pragma once

/**
 * Resource extension: UResource types, catalog, package IO, BulkData kick-off.
 * Type-specific work is IResourceImporter / IResourceExporter (private ResourceIO).
 */

#include <Core/DependsPack.h>
#include <Core/Export.h>
#include <Core/Extension/GC/GC.h>
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

namespace Maho
{

class UPackage;
class FGCSystem;
class FResourceSystem;
class FResourceServer;
class IResourceImporter;
class IResourceExporter;
void RegisterGeneratedResourceTypes(FResourceSystem& Manager, FGCSystem& GC);

MAHO_ENUM()
enum class EResourceLoadState : std::uint8_t
{
	Invalid = 0,
	Pending,
	Ready,
	Failed
};

/** High-level asset kind (extend as typed resources appear). */
MAHO_ENUM()
enum class EResourceType : std::uint8_t
{
	Unknown = 0,
	Raw,
	/** Legacy alias — Prefer Texture2D. Kept for package/string compat. */
	Texture,
	Texture2D,
	Texture3D,
	TextureCube,
	TextureCubeArray,
	Texture2DArray,
	Mesh,
	Material,
	Skeleton,
	/** Imported single-clip animation (Assimp aiAnimation). */
	Animation,
	/** Package-inline blend/orchestration of UAnimation SoftPaths. */
	AnimationGraph,
	/** Prefab (assembly JSON); may carry scene Metadata including coordinate system. */
	Prefab,
	Shader,
	Audio,
	Data,
};

/** Source asset up-axis (Prefab Metadata / FDecodedModelMetadata). */
MAHO_ENUM()
enum class EModelAxis : std::uint8_t
{
	Unknown = 0,
	X,
	Y,
	Z,
	NegX,
	NegY,
	NegZ,
};

MAHO_ENUM()
enum class EModelHandedness : std::uint8_t
{
	Unknown = 0,
	Right,
	Left,
};

/**
 * CPU-side texture dimension (Game thread). Not an RHI/GPU type.
 * U* assets never hold GPU resources — Render snapshots create FRHITexture later.
 */
MAHO_ENUM()
enum class ETextureDimension : std::uint8_t
{
	Tex2D = 0,
	Tex3D,
	Cube,
	CubeArray,
	Tex2DArray,
};

/**
 * CPU pixel layout for imported/exported texture bytes (Game thread only).
 * Values may later align numerically with RHI formats; Public must not include RHI headers.
 */
MAHO_ENUM()
enum class ETexturePixelFormat : std::uint8_t
{
	Unknown = 0,
	R8,
	RG8,
	RGB8,
	RGBA8,
	RGBA16F,
	RGBA32F,
	/** Opaque GPU-compressed payload retained for later Render upload (e.g. from KTX2). */
	BlockCompressed,
};

/**
 * External-file asset (png / mesh / ...).
 * Outer may be an UPackage (saved content) or null (runtime-only; PathName = ObjectName).
 * Created by the private Resource module; LoadState becomes Ready after BulkData import.
 */
MAHO_OBJECT()
class MAHO_API UResource : public UObject
{
	MAHO_GENERATED_BODY()

public:
	static constexpr int PoolSize = 64;

	UResource(
		UPackage* InOuter,
		std::string InObjectName,
		EResourceType InType,
		std::string InSourcePath);
	virtual ~UResource() override;

	void OnPoolTearDown() override;

	MAHO_FUNCTION()
	[[nodiscard]] EResourceType GetType() const { return Type; }
	MAHO_FUNCTION()
	[[nodiscard]] const std::string& GetSourcePath() const { return SourcePath; }
	/** Alias for GetSourcePath (legacy name). */
	MAHO_FUNCTION()
	[[nodiscard]] const std::string& GetPath() const { return SourcePath; }
	MAHO_FUNCTION()
	[[nodiscard]] EResourceLoadState GetLoadState() const { return LoadState; }

	/** Mark CPU payload ready (sibling assets created during scene Apply, authored content). */
	void MarkCpuReady() { SetLoadState(EResourceLoadState::Ready); }

protected:
	friend class FResourceSystem;

	void SetLoadState(EResourceLoadState InState) { LoadState = InState; }

	MAHO_PROPERTY()
	EResourceType Type = EResourceType::Unknown;
	MAHO_PROPERTY()
	std::string SourcePath;
	MAHO_PROPERTY()
	EResourceLoadState LoadState = EResourceLoadState::Pending;
};

/**
 * Game-thread texture asset: CPU pixels / BulkData only.
 * Must not hold FRHI* / Vk* / GPU handles. Render creates GPU textures from a snapshot later.
 * Not MAHO_OBJECT — Lua FLua_* wrappers only support UObject as sol base today.
 * Formerly UTextureResource (removed).
 */
class MAHO_API UTexture : public UResource
{
public:
	static constexpr int PoolSize = 32;

	UTexture(
		UPackage* InOuter,
		std::string InObjectName,
		EResourceType InType,
		std::string InSourcePath);
	~UTexture() override;

	[[nodiscard]] ETextureDimension GetDimension() const { return Dimension; }
	[[nodiscard]] ETexturePixelFormat GetPixelFormat() const { return PixelFormat; }
	[[nodiscard]] std::uint32_t GetWidth() const { return Width; }
	[[nodiscard]] std::uint32_t GetHeight() const { return Height; }
	[[nodiscard]] std::uint32_t GetDepth() const { return Depth; }
	[[nodiscard]] std::uint32_t GetArrayLayers() const { return ArrayLayers; }
	[[nodiscard]] std::uint32_t GetMipCount() const { return MipCount; }
	[[nodiscard]] bool IsSRGB() const { return bSRGB; }
	[[nodiscard]] const std::vector<std::uint8_t>& GetPixels() const { return Pixels; }
	[[nodiscard]] std::vector<std::uint8_t>& GetPixelsMutable() { return Pixels; }
	[[nodiscard]] std::uint64_t GetContentGeneration() const { return ContentGeneration; }

	void SetCpuImage(
		ETextureDimension InDimension,
		ETexturePixelFormat InFormat,
		std::uint32_t InWidth,
		std::uint32_t InHeight,
		std::uint32_t InDepth,
		std::uint32_t InArrayLayers,
		std::uint32_t InMipCount,
		bool bInSRGB,
		std::vector<std::uint8_t> InPixels);

protected:
	ETextureDimension Dimension = ETextureDimension::Tex2D;
	ETexturePixelFormat PixelFormat = ETexturePixelFormat::Unknown;
	std::uint32_t Width = 0;
	std::uint32_t Height = 0;
	std::uint32_t Depth = 1;
	std::uint32_t ArrayLayers = 1;
	std::uint32_t MipCount = 1;
	bool bSRGB = true;
	std::vector<std::uint8_t> Pixels;
	std::uint64_t ContentGeneration = 0;
};

class MAHO_API UTexture2D : public UTexture
{
public:
	static constexpr int PoolSize = 32;
	UTexture2D(UPackage* InOuter, std::string InObjectName, EResourceType InType, std::string InSourcePath);
	~UTexture2D() override;
};

class MAHO_API UTexture3D : public UTexture
{
public:
	static constexpr int PoolSize = 16;
	UTexture3D(UPackage* InOuter, std::string InObjectName, EResourceType InType, std::string InSourcePath);
	~UTexture3D() override;
};

class MAHO_API UTextureCube : public UTexture
{
public:
	static constexpr int PoolSize = 16;
	UTextureCube(UPackage* InOuter, std::string InObjectName, EResourceType InType, std::string InSourcePath);
	~UTextureCube() override;
};

class MAHO_API UTextureCubeArray : public UTexture
{
public:
	static constexpr int PoolSize = 8;
	UTextureCubeArray(UPackage* InOuter, std::string InObjectName, EResourceType InType, std::string InSourcePath);
	~UTextureCubeArray() override;
};

class MAHO_API UTexture2DArray : public UTexture
{
public:
	static constexpr int PoolSize = 16;
	UTexture2DArray(UPackage* InOuter, std::string InObjectName, EResourceType InType, std::string InSourcePath);
	~UTexture2DArray() override;
};

/** CPU material: SoftPaths to textures + simple scalars (Game thread only). */
class MAHO_API UMaterial : public UResource
{
public:
	static constexpr int PoolSize = 32;

	UMaterial(UPackage* InOuter, std::string InObjectName, EResourceType InType, std::string InSourcePath);
	~UMaterial() override;

	[[nodiscard]] const FSoftObjectPath& GetBaseColorTexture() const { return BaseColorTexture; }
	void SetBaseColorTexture(FSoftObjectPath Path) { BaseColorTexture = std::move(Path); }
	[[nodiscard]] const FSoftObjectPath& GetNormalTexture() const { return NormalTexture; }
	void SetNormalTexture(FSoftObjectPath Path) { NormalTexture = std::move(Path); }
	[[nodiscard]] const FSoftObjectPath& GetMetallicRoughnessTexture() const { return MetallicRoughnessTexture; }
	void SetMetallicRoughnessTexture(FSoftObjectPath Path) { MetallicRoughnessTexture = std::move(Path); }

	float BaseColorFactor[4] = {1.f, 1.f, 1.f, 1.f};
	float MetallicFactor = 0.f;
	float RoughnessFactor = 1.f;

protected:
	FSoftObjectPath BaseColorTexture;
	FSoftObjectPath NormalTexture;
	FSoftObjectPath MetallicRoughnessTexture;
};

/** CPU static mesh geometry + SoftPath to UMaterial. */
class MAHO_API UStaticMesh : public UResource
{
public:
	static constexpr int PoolSize = 32;

	UStaticMesh(UPackage* InOuter, std::string InObjectName, EResourceType InType, std::string InSourcePath);
	~UStaticMesh() override;

	[[nodiscard]] const FSoftObjectPath& GetMaterial() const { return Material; }
	void SetMaterial(FSoftObjectPath Path) { Material = std::move(Path); }
	[[nodiscard]] const std::vector<float>& GetPositions() const { return Positions; }
	[[nodiscard]] const std::vector<float>& GetNormals() const { return Normals; }
	[[nodiscard]] const std::vector<float>& GetUVs() const { return UVs; }
	[[nodiscard]] const std::vector<std::uint32_t>& GetIndices() const { return Indices; }
	[[nodiscard]] std::uint64_t GetContentGeneration() const { return ContentGeneration; }

	void SetCpuGeometry(
		std::vector<float> InPositions,
		std::vector<float> InNormals,
		std::vector<float> InUVs,
		std::vector<std::uint32_t> InIndices);

protected:
	FSoftObjectPath Material;
	std::vector<float> Positions;
	std::vector<float> Normals;
	std::vector<float> UVs;
	std::vector<std::uint32_t> Indices;
	std::uint64_t ContentGeneration = 0;
};

struct FSkeletonBone
{
	std::string Name;
	std::int32_t ParentIndex = -1;
	/** Row-major 4x4 bind local matrix. */
	float BindLocal[16] = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1};
};

class MAHO_API USkeleton : public UResource
{
public:
	static constexpr int PoolSize = 16;

	USkeleton(UPackage* InOuter, std::string InObjectName, EResourceType InType, std::string InSourcePath);
	~USkeleton() override;

	[[nodiscard]] const std::vector<FSkeletonBone>& GetBones() const { return Bones; }
	[[nodiscard]] std::uint64_t GetContentGeneration() const { return ContentGeneration; }
	void SetBones(std::vector<FSkeletonBone> InBones);

protected:
	std::vector<FSkeletonBone> Bones;
	std::uint64_t ContentGeneration = 0;
};

struct FAnimationKey
{
	float Time = 0.f;
	float Translation[3] = {0, 0, 0};
	float Rotation[4] = {0, 0, 0, 1}; // xyzw
	float Scale[3] = {1, 1, 1};
};

struct FAnimationTrack
{
	std::string TargetBoneName;
	std::vector<FAnimationKey> Keys;
};

/** Imported single animation clip (CPU tracks). */
class MAHO_API UAnimation : public UResource
{
public:
	static constexpr int PoolSize = 32;

	UAnimation(UPackage* InOuter, std::string InObjectName, EResourceType InType, std::string InSourcePath);
	~UAnimation() override;

	[[nodiscard]] const FSoftObjectPath& GetSkeleton() const { return Skeleton; }
	void SetSkeleton(FSoftObjectPath Path);
	[[nodiscard]] float GetDurationSeconds() const { return DurationSeconds; }
	void SetDurationSeconds(float Seconds);
	[[nodiscard]] const std::vector<FAnimationTrack>& GetTracks() const { return Tracks; }
	void SetTracks(std::vector<FAnimationTrack> InTracks);
	[[nodiscard]] std::uint64_t GetContentGeneration() const { return ContentGeneration; }

protected:
	FSoftObjectPath Skeleton;
	float DurationSeconds = 0.f;
	std::vector<FAnimationTrack> Tracks;
	std::uint64_t ContentGeneration = 0;
};

/**
 * Package-inline orchestration of UAnimation SoftPaths (default blend config).
 * Authoritative DocumentJson; SourcePath may be empty.
 */
class MAHO_API UAnimationGraph : public UResource
{
public:
	static constexpr int PoolSize = 16;

	UAnimationGraph(UPackage* InOuter, std::string InObjectName, EResourceType InType, std::string InSourcePath);
	~UAnimationGraph() override;

	[[nodiscard]] const std::string& GetDocumentJson() const { return DocumentJson; }
	void SetDocumentJson(std::string Json) { DocumentJson = std::move(Json); }

protected:
	std::string DocumentJson;
};

/**
 * Prefab (assembly). Package-inline JSON: Meshes[], Skeleton, AnimationGraph, Metadata.
 * SourcePath may be empty for authored prefabs; model import sets SourcePath to the scene file.
 */
class MAHO_API UPrefab : public UResource
{
public:
	static constexpr int PoolSize = 16;

	UPrefab(UPackage* InOuter, std::string InObjectName, EResourceType InType, std::string InSourcePath);
	~UPrefab() override;

	[[nodiscard]] const std::string& GetDocumentJson() const { return DocumentJson; }
	void SetDocumentJson(std::string Json) { DocumentJson = std::move(Json); }

protected:
	std::string DocumentJson;
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
class MAHO_API FResourceSystem final
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

	/**
	 * Package name-table + catalog for a newly created sibling (e.g. mesh scene Apply).
	 * Prefers this over calling UPackage::RegisterObject from codecs (that API is private).
	 */
	[[nodiscard]] bool RegisterOwnedResource(UPackage& Package, const FObjectRef& Resource);

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

	/** SoftPath / catalog join key (also used by Game→Render proxy registries). */
	[[nodiscard]] static std::string MakeResourceCatalogKey(const UResource& Resource);
	[[nodiscard]] static std::string NormalizeResourceVirtualPath(const std::string& VirtualPath);

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

} // namespace Maho
