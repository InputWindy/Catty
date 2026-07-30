#pragma once

#include <Core/Export.h>
#include <Core/Module.h>
#include <Core/Object.h>
#include <Core/ObjectReflect.h>
#include <Core/SoftObjectPath.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Catty
{

class FPackage;
class FResourceServer;

// ---------------------------------------------------------------------------
// Async load identity / state
// ---------------------------------------------------------------------------

/** Opaque resource identity for async raw loads (issued by ResourceManager server). */
CATTY_STRUCT()
struct FResourceId
{
	CATTY_GENERATED_STRUCT_BODY()

	CATTY_PROPERTY()
	std::uint64_t Value = 0;

	[[nodiscard]] bool IsValid() const { return Value != 0; }

	friend bool operator==(FResourceId A, FResourceId B) { return A.Value == B.Value; }
	friend bool operator!=(FResourceId A, FResourceId B) { return A.Value != B.Value; }
};

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
 * Outer may be an FPackage (saved content) or null (runtime-only; PathName = ObjectName).
 * The FResource object is created synchronously; raw bytes are filled later by
 * the ResourceManager loader (Pending → Ready/Failed).
 * Named FResource (asset) vs FResourceManager (IModule) — different types.
 */
CATTY_OBJECT()
class CATTY_API FResource : public FObject
{
	CATTY_GENERATED_BODY()

public:
	static constexpr int PoolSize = 64;

	FResource(
		FPackage* InOuter,
		std::string InObjectName,
		FResourceId InId,
		EResourceType InType,
		std::string InSourcePath);
	virtual ~FResource() override;

	static void StaticTearDown(FResource* Resource);

	[[nodiscard]] FResourceId GetId() const { return Id; }

	CATTY_FUNCTION()
	[[nodiscard]] std::uint64_t GetIdValue() const { return Id.Value; }
	CATTY_FUNCTION()
	[[nodiscard]] EResourceType GetType() const { return Type; }
	CATTY_FUNCTION()
	[[nodiscard]] const std::string& GetSourcePath() const { return SourcePath; }
	/** Alias for GetSourcePath (legacy name). */
	CATTY_FUNCTION()
	[[nodiscard]] const std::string& GetPath() const { return SourcePath; }

protected:
	CATTY_PROPERTY()
	EResourceType Type = EResourceType::Unknown;
	CATTY_PROPERTY()
	std::string SourcePath;

	FResourceId Id{};
};

/**
 * Built-in Resource module: catalog + package IO.
 * Owns PathName → FObjectRef catalog as load roots only.
 * Object / package residency queries go through FGC (LiveObjects).
 */
class CATTY_API FResourceManager final : public IModule
{
public:
	FResourceManager();
	~FResourceManager() override;

	FResourceManager(const FResourceManager&) = delete;
	FResourceManager& operator=(const FResourceManager&) = delete;

	const char* GetName() const override { return "Resource"; }

	void GetDependencies(EModuleStage Stage, std::vector<std::string>& OutNames) const override
	{
		switch (Stage)
		{
		case EModuleStage::PreInit:
		case EModuleStage::Init:
		case EModuleStage::PostInit:
			OutNames.push_back("GC");
			break;
		default:
			break;
		}
	}

	bool ExecuteStage(EModuleStage Stage, FApp& App, FStageContext& Ctx) override;
	[[nodiscard]] bool IsIdle() const override;

	[[nodiscard]] bool Initialize();
	void Shutdown();
	[[nodiscard]] bool IsInitialized() const;

	void PrepareForExit();

	[[nodiscard]] bool RegisterResource(const FObjectRef& Resource);
	bool UnregisterResource(FObject* Resource);
	bool UnregisterResource(const FObjectRef& Resource);

	bool UnloadResource(const std::string& VirtualPath);
	bool UnloadResource(const FObjectRef& Resource);

	[[nodiscard]] FObjectRef TryLoad(const FSoftObjectPath& SoftPath);
	[[nodiscard]] FObjectRef TryLoad(const std::string& SoftPathString);

	void ReleaseResourceId(FResourceId Id);

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

private:
	[[nodiscard]] static std::string NormalizePackageName(std::string Name);
	[[nodiscard]] static std::string NormalizeSourcePath(std::string Path);
	[[nodiscard]] static std::string MakeObjectNameFromSource(const std::string& SourcePath);
	[[nodiscard]] static EResourceType InferTypeFromPath(const std::string& Path);
	[[nodiscard]] static EResourceType ResourceTypeFromString(const std::string& Name);
	[[nodiscard]] static std::string MakeResourceCatalogKey(const FResource& Resource);
	[[nodiscard]] static std::string NormalizeResourceVirtualPath(const std::string& VirtualPath);

	void UnregisterResourcesInPackage(const std::string& PackageName);

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
	bool bAcceptingNewWork = true;
};

namespace Detail
{
[[nodiscard]] CATTY_API FResourceManager* GetResourceManager();
}

} // namespace Catty
