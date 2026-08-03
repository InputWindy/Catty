#pragma once

/**
 * Explicit Importer / Exporter types for FResourceSystem::Import / Export.
 * Callers pass TResourceImporter<T> / TResourceExporter<T> / FCassetPackageImporter as templates.
 * No global RegisterImporter table — Manager stores only the instance for an in-flight SoftPath.
 */

#include <Core/Extension/Resource/Resource.h>

#include <Core/System/Log.h>
#include <Core/Extension/GC/GC.h>
#include <Core/Object/Package.h>
#include <Core/System/Paths.h>

#include <type_traits>
#include <utility>

namespace Maho
{

class MAHO_API IResourceImporter
{
public:
	virtual ~IResourceImporter() = default;

	[[nodiscard]] virtual EResourceType GetType() const = 0;
	[[nodiscard]] virtual bool MatchesSourcePath(const std::string& SourcePath) const = 0;

	/**
	 * Game-thread Apply after BulkData Succeeded.
	 * Creates GC objects as needed; returns false on failure.
	 */
	[[nodiscard]] virtual bool ApplyBulkData(
		FResourceSystem& Manager,
		FResourceImportConfig& Config,
		FResourceBulkData& Bulk) = 0;
};

class IResourceExporter
{
public:
	virtual ~IResourceExporter() = default;

	[[nodiscard]] virtual EResourceType GetType() const = 0;
	[[nodiscard]] virtual bool CanExport(const FObjectRef& Resource) const = 0;
	[[nodiscard]] virtual bool Export(FResourceExportConfig Config, const FObjectRef& Resource) = 0;
};

/** .casset package hydrate (MCAS binary BulkData → LoadPackageFromBinary). */
class MAHO_API FCassetPackageImporter final : public IResourceImporter
{
public:
	[[nodiscard]] EResourceType GetType() const override
	{
		return EResourceType::Data;
	}

	[[nodiscard]] bool MatchesSourcePath(const std::string& SourcePath) const override;

	[[nodiscard]] bool ApplyBulkData(
		FResourceSystem& Manager,
		FResourceImportConfig& Config,
		FResourceBulkData& Bulk) override;
};

template <typename TResource>
struct TResourceIOTraits
{
	static_assert(sizeof(TResource) == 0, "Specialize TResourceIOTraits for this resource type");
};

template <typename TResource>
class TResourceImporter final : public IResourceImporter
{
public:
	static_assert(std::is_base_of_v<UResource, TResource>, "TResource must derive from UResource");

	using FTraits = TResourceIOTraits<TResource>;

	[[nodiscard]] EResourceType GetType() const override
	{
		return FTraits::GetType();
	}

	[[nodiscard]] bool MatchesSourcePath(const std::string& SourcePath) const override
	{
		return FTraits::MatchesSourcePath(SourcePath);
	}

	[[nodiscard]] bool ApplyBulkData(
		FResourceSystem& Manager,
		FResourceImportConfig& Config,
		FResourceBulkData& Bulk) override
	{
		if (Config.TypeHint == EResourceType::Unknown)
		{
			Config.TypeHint = FTraits::GetType();
		}
		return Manager.ApplyTypedBulkData<TResource>(Config, Bulk);
	}
};

template <typename TResource>
class TResourceExporter final : public IResourceExporter
{
public:
	static_assert(std::is_base_of_v<UResource, TResource>, "TResource must derive from UResource");

	using FTraits = TResourceIOTraits<TResource>;

	[[nodiscard]] EResourceType GetType() const override
	{
		return FTraits::GetType();
	}

	[[nodiscard]] bool CanExport(const FObjectRef& Resource) const override
	{
		return Resource.Cast<TResource>() != nullptr;
	}

	[[nodiscard]] bool Export(FResourceExportConfig Config, const FObjectRef& Resource) override
	{
		TResource* Typed = Resource.Cast<TResource>();
		if (!Typed)
		{
			MAHO_CORE_ERROR("TResourceExporter: Ref is not the expected resource type");
			return false;
		}

		if (Config.DestinationPath.empty())
		{
			MAHO_CORE_ERROR("TResourceExporter: empty DestinationPath");
			return false;
		}

		return FTraits::ExportSource(Config, *Typed);
	}
};

template <>
struct MAHO_API TResourceIOTraits<UResource>
{
	static constexpr EResourceType GetType()
	{
		return EResourceType::Raw;
	}

	/** Names accepted by ResourceTypeFromString / package class field. */
	static constexpr const char* TypeNames[] = {
		"Raw",
		"Resource",
		"Object",
		"UResource",
	};

	[[nodiscard]] static bool MatchesSourcePath(const std::string& SourcePath)
	{
		(void)SourcePath;
		return true;
	}

	[[nodiscard]] static bool ImportSource(
		FResourceImportConfig& Config,
		FResourceBulkData& Bulk,
		UResource& Resource);

	[[nodiscard]] static bool ExportSource(FResourceExportConfig& Config, const UResource& Resource);
};

template <>
struct MAHO_API TResourceIOTraits<UTexture2D>
{
	static constexpr EResourceType GetType() { return EResourceType::Texture2D; }
	static constexpr const char* TypeNames[] = {
		"Texture2D",
		"Texture",
		"UTexture2D",
		"UTexture",
		"TextureResource",
		"UTextureResource",
	};
	[[nodiscard]] static bool MatchesSourcePath(const std::string& SourcePath);
	[[nodiscard]] static bool ImportSource(
		FResourceImportConfig& Config,
		FResourceBulkData& Bulk,
		UTexture2D& Resource);
	[[nodiscard]] static bool ExportSource(FResourceExportConfig& Config, const UTexture2D& Resource);
};

template <>
struct MAHO_API TResourceIOTraits<UTexture3D>
{
	static constexpr EResourceType GetType() { return EResourceType::Texture3D; }
	static constexpr const char* TypeNames[] = { "Texture3D", "UTexture3D" };
	[[nodiscard]] static bool MatchesSourcePath(const std::string& SourcePath);
	[[nodiscard]] static bool ImportSource(
		FResourceImportConfig& Config,
		FResourceBulkData& Bulk,
		UTexture3D& Resource);
	[[nodiscard]] static bool ExportSource(FResourceExportConfig& Config, const UTexture3D& Resource);
};

template <>
struct MAHO_API TResourceIOTraits<UTextureCube>
{
	static constexpr EResourceType GetType() { return EResourceType::TextureCube; }
	static constexpr const char* TypeNames[] = { "TextureCube", "UTextureCube", "Cubemap" };
	[[nodiscard]] static bool MatchesSourcePath(const std::string& SourcePath);
	[[nodiscard]] static bool ImportSource(
		FResourceImportConfig& Config,
		FResourceBulkData& Bulk,
		UTextureCube& Resource);
	[[nodiscard]] static bool ExportSource(FResourceExportConfig& Config, const UTextureCube& Resource);
};

template <>
struct MAHO_API TResourceIOTraits<UTextureCubeArray>
{
	static constexpr EResourceType GetType() { return EResourceType::TextureCubeArray; }
	static constexpr const char* TypeNames[] = { "TextureCubeArray", "UTextureCubeArray" };
	[[nodiscard]] static bool MatchesSourcePath(const std::string& SourcePath);
	[[nodiscard]] static bool ImportSource(
		FResourceImportConfig& Config,
		FResourceBulkData& Bulk,
		UTextureCubeArray& Resource);
	[[nodiscard]] static bool ExportSource(FResourceExportConfig& Config, const UTextureCubeArray& Resource);
};

template <>
struct MAHO_API TResourceIOTraits<UTexture2DArray>
{
	static constexpr EResourceType GetType() { return EResourceType::Texture2DArray; }
	static constexpr const char* TypeNames[] = { "Texture2DArray", "UTexture2DArray" };
	[[nodiscard]] static bool MatchesSourcePath(const std::string& SourcePath);
	[[nodiscard]] static bool ImportSource(
		FResourceImportConfig& Config,
		FResourceBulkData& Bulk,
		UTexture2DArray& Resource);
	[[nodiscard]] static bool ExportSource(FResourceExportConfig& Config, const UTexture2DArray& Resource);
};

/**
 * Model scene import root: fbx/gltf/obj/... → Decode → Apply siblings + Prefab DocumentJson.
 * SoftPaths to Meshes / Skeleton / AnimationGraph live in Prefab JSON (Metadata peer).
 */
template <>
struct MAHO_API TResourceIOTraits<UPrefab>
{
	static constexpr EResourceType GetType() { return EResourceType::Prefab; }
	static constexpr const char* TypeNames[] = { "Prefab", "UPrefab", "Model", "MeshScene" };
	[[nodiscard]] static bool MatchesSourcePath(const std::string& SourcePath);
	[[nodiscard]] static bool ImportSource(
		FResourceImportConfig& Config,
		FResourceBulkData& Bulk,
		UPrefab& Resource);
	[[nodiscard]] static bool ExportSource(FResourceExportConfig& Config, const UPrefab& Resource);
};

/** Sibling CPU types created by Prefab scene Apply (no direct file Match). */
template <>
struct TResourceIOTraits<UMaterial>
{
	static constexpr EResourceType GetType() { return EResourceType::Material; }
	static constexpr const char* TypeNames[] = { "Material", "UMaterial" };
	[[nodiscard]] static bool MatchesSourcePath(const std::string& SourcePath);
	[[nodiscard]] static bool ImportSource(
		FResourceImportConfig& Config,
		FResourceBulkData& Bulk,
		UMaterial& Resource);
	[[nodiscard]] static bool ExportSource(FResourceExportConfig& Config, const UMaterial& Resource);
};

template <>
struct TResourceIOTraits<UStaticMesh>
{
	static constexpr EResourceType GetType() { return EResourceType::Mesh; }
	static constexpr const char* TypeNames[] = { "Mesh", "StaticMesh", "UStaticMesh" };
	[[nodiscard]] static bool MatchesSourcePath(const std::string& SourcePath);
	[[nodiscard]] static bool ImportSource(
		FResourceImportConfig& Config,
		FResourceBulkData& Bulk,
		UStaticMesh& Resource);
	[[nodiscard]] static bool ExportSource(FResourceExportConfig& Config, const UStaticMesh& Resource);
};

template <>
struct TResourceIOTraits<USkeleton>
{
	static constexpr EResourceType GetType() { return EResourceType::Skeleton; }
	static constexpr const char* TypeNames[] = { "Skeleton", "USkeleton" };
	[[nodiscard]] static bool MatchesSourcePath(const std::string& SourcePath);
	[[nodiscard]] static bool ImportSource(
		FResourceImportConfig& Config,
		FResourceBulkData& Bulk,
		USkeleton& Resource);
	[[nodiscard]] static bool ExportSource(FResourceExportConfig& Config, const USkeleton& Resource);
};

template <>
struct TResourceIOTraits<UAnimation>
{
	static constexpr EResourceType GetType() { return EResourceType::Animation; }
	static constexpr const char* TypeNames[] = { "Animation", "UAnimation" };
	[[nodiscard]] static bool MatchesSourcePath(const std::string& SourcePath);
	[[nodiscard]] static bool ImportSource(
		FResourceImportConfig& Config,
		FResourceBulkData& Bulk,
		UAnimation& Resource);
	[[nodiscard]] static bool ExportSource(FResourceExportConfig& Config, const UAnimation& Resource);
};

template <>
struct TResourceIOTraits<UAnimationGraph>
{
	static constexpr EResourceType GetType() { return EResourceType::AnimationGraph; }
	static constexpr const char* TypeNames[] = { "AnimationGraph", "UAnimationGraph" };
	[[nodiscard]] static bool MatchesSourcePath(const std::string& SourcePath);
	[[nodiscard]] static bool ImportSource(
		FResourceImportConfig& Config,
		FResourceBulkData& Bulk,
		UAnimationGraph& Resource);
	[[nodiscard]] static bool ExportSource(FResourceExportConfig& Config, const UAnimationGraph& Resource);
};

template <typename TImporter>
FSoftObjectPath FResourceSystem::Import(FResourceImportConfig Config)
{
	return EnqueueImport(std::make_unique<TImporter>(), std::move(Config));
}

template <typename TResource>
bool FResourceSystem::ApplyTypedBulkData(FResourceImportConfig& Config, FResourceBulkData& Bulk)
{
	using FTraits = TResourceIOTraits<TResource>;

	if (!IsInitialized() || !bAcceptingNewWork)
	{
		MAHO_CORE_ERROR("FResourceSystem::ApplyTypedBulkData: not accepting work");
		return false;
	}

	Config.SourcePath = NormalizeSourcePath(std::move(Config.SourcePath));
	Config.PackagePath = NormalizePackageName(std::move(Config.PackagePath));
	if (Config.ObjectName.empty() && !Config.SourcePath.empty())
	{
		Config.ObjectName = MakeObjectNameFromSource(Config.SourcePath);
	}

	if (Config.PackagePath.empty() || Config.ObjectName.empty() || Config.SourcePath.empty())
	{
		MAHO_CORE_ERROR("FResourceSystem::ApplyTypedBulkData: PackagePath/ObjectName/SourcePath required");
		return false;
	}

	FGCSystem* GC = Detail::GetGCSystem();
	if (!GC)
	{
		MAHO_CORE_ERROR("FResourceSystem::ApplyTypedBulkData: GC unavailable");
		return false;
	}

	FObjectRef PackageRef = GC->FindPackage(Config.PackagePath);
	if (!PackageRef)
	{
		PackageRef = GC->NewObject<UPackage>(Config.PackagePath, EPackageFlags::Persistent);
	}
	UPackage* PackagePtr = PackageRef.Cast<UPackage>();
	if (!PackagePtr)
	{
		MAHO_CORE_ERROR(
			"FResourceSystem::ApplyTypedBulkData: failed to create package '{}'",
			Config.PackagePath);
		return false;
	}

	if (PackagePtr->FindObject(Config.ObjectName))
	{
		MAHO_CORE_ERROR(
			"FResourceSystem::ApplyTypedBulkData: '{}' already exists in '{}'",
			Config.ObjectName,
			PackagePtr->GetName());
		return false;
	}

	EResourceType Type = Config.TypeHint;
	if (Type == EResourceType::Unknown)
	{
		Type = FTraits::GetType();
	}

	FObjectRef ResourceRef = GC->NewObject<TResource>(
		PackagePtr,
		Config.ObjectName,
		Type,
		Config.SourcePath);
	TResource* Resource = ResourceRef.Cast<TResource>();
	if (!Resource)
	{
		MAHO_CORE_ERROR("FResourceSystem::ApplyTypedBulkData: NewObject failed");
		return false;
	}

	Resource->SetLoadState(EResourceLoadState::Pending);
	if (!RegisterOwnedResource(*PackagePtr, ResourceRef))
	{
		Resource->ClearOuter();
		return false;
	}

	const bool bOk = FTraits::ImportSource(Config, Bulk, *Resource);
	Resource->SetLoadState(bOk ? EResourceLoadState::Ready : EResourceLoadState::Failed);
	if (!bOk)
	{
		AbortFailedImport(*Resource);
		return false;
	}

	Resource->MarkDirty();
	if (UPackage* Package = Resource->GetPackage().Cast<UPackage>())
	{
		for (const auto& Pair : Package->Objects)
		{
			if (UResource* Sibling = dynamic_cast<UResource*>(Pair.second))
			{
				Sibling->MarkDirty();
			}
		}
	}
	return true;
}

template <typename TExporter>
FSoftObjectPath FResourceSystem::Export(FResourceExportConfig Config, const FSoftObjectPath& Source)
{
	if (!IsInitialized() || !bAcceptingNewWork)
	{
		MAHO_CORE_ERROR("FResourceSystem::Export: not accepting work");
		return {};
	}

	FObjectRef Resource = Source.Resolve();
	if (!Resource)
	{
		MAHO_CORE_ERROR("FResourceSystem::Export: SoftPath '{}' not loaded", Source.ToString());
		return {};
	}

	TExporter Exporter;
	if (!Exporter.CanExport(Resource) || !Exporter.Export(std::move(Config), Resource))
	{
		return {};
	}

	return Source;
}

} // namespace Maho
