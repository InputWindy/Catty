#pragma once

/**
 * Polymorphic Importer / Exporter for the private Resource module.
 * Add a resource type: inherit UResource, specialize TResourceIOTraits, register
 * TResourceImporter<T> / TResourceExporter<T> (see RegisterDefaultImportersAndExporters).
 * FResourceSystem only holds IResourceImporter* / IResourceExporter* — no type switch.
 */

#include <Core/Extension/Resource/Resource.h>

#include <Core/System/Log.h>
#include <Core/Extension/GC/GC.h>
#include <Core/Object/Package.h>

#include <type_traits>
#include <utility>

namespace Maho
{

class IResourceImporter
{
public:
	virtual ~IResourceImporter() = default;

	[[nodiscard]] virtual EResourceType GetType() const = 0;
	[[nodiscard]] virtual bool MatchesSourcePath(const std::string& SourcePath) const = 0;
	[[nodiscard]] virtual FObjectRef Import(FResourceSystem& Manager, FResourceImportConfig Config) = 0;
	[[nodiscard]] virtual bool ApplyBulkData(
		FResourceImportConfig& Config,
		FResourceBulkData& Bulk,
		FObjectRef& Resource) = 0;
};

class IResourceExporter
{
public:
	virtual ~IResourceExporter() = default;

	[[nodiscard]] virtual EResourceType GetType() const = 0;
	[[nodiscard]] virtual bool CanExport(const FObjectRef& Resource) const = 0;
	[[nodiscard]] virtual bool Export(FResourceExportConfig Config, const FObjectRef& Resource) = 0;
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

	[[nodiscard]] FObjectRef Import(FResourceSystem& Manager, FResourceImportConfig Config) override
	{
		if (Config.TypeHint == EResourceType::Unknown)
		{
			Config.TypeHint = FTraits::GetType();
		}
		return Manager.BeginImport<TResource>(Config, this);
	}

	[[nodiscard]] bool ApplyBulkData(
		FResourceImportConfig& Config,
		FResourceBulkData& Bulk,
		FObjectRef& Resource) override
	{
		TResource* Typed = Resource.Cast<TResource>();
		if (!Typed)
		{
			MAHO_CORE_ERROR("TResourceImporter::ApplyBulkData: unexpected resource type");
			return false;
		}
		return FTraits::ImportSource(Config, Bulk, *Typed);
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
struct TResourceIOTraits<UResource>
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
struct TResourceIOTraits<UTexture2D>
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
struct TResourceIOTraits<UTexture3D>
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
struct TResourceIOTraits<UTextureCube>
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
struct TResourceIOTraits<UTextureCubeArray>
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
struct TResourceIOTraits<UTexture2DArray>
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

template <typename TResource>
FObjectRef FResourceSystem::BeginImport(FResourceImportConfig& Config, IResourceImporter* Importer)
{
	if (!Importer)
	{
		MAHO_CORE_ERROR("FResourceSystem::BeginImport: null Importer");
		return {};
	}

	if (!IsInitialized())
	{
		MAHO_CORE_ERROR("FResourceSystem::BeginImport: not initialized");
		return {};
	}

	if (!bAcceptingNewWork)
	{
		MAHO_CORE_ERROR("FResourceSystem::BeginImport: refused during exit");
		return {};
	}

	if (!Config.Package)
	{
		MAHO_CORE_ERROR("FResourceSystem::BeginImport: invalid Package");
		return {};
	}

	UPackage* PackagePtr = Config.Package.Cast<UPackage>();
	if (!PackagePtr)
	{
		MAHO_CORE_ERROR("FResourceSystem::BeginImport: Ref is not an UPackage");
		return {};
	}

	UPackage& PackageObj = *PackagePtr;

	Config.SourcePath = NormalizeSourcePath(std::move(Config.SourcePath));
	if (Config.ObjectName.empty() && !Config.SourcePath.empty())
	{
		Config.ObjectName = MakeObjectNameFromSource(Config.SourcePath);
	}

	if (Config.ObjectName.empty())
	{
		MAHO_CORE_ERROR("FResourceSystem::BeginImport: empty ObjectName");
		return {};
	}

	if (Config.SourcePath.empty())
	{
		MAHO_CORE_ERROR("FResourceSystem::BeginImport: empty SourcePath");
		return {};
	}

	if (PackageObj.FindObject(Config.ObjectName))
	{
		MAHO_CORE_ERROR(
			"FResourceSystem::BeginImport: '{}' already exists in '{}'",
			Config.ObjectName,
			PackageObj.GetName());
		return {};
	}

	EResourceType Type = Config.TypeHint;
	if (Type == EResourceType::Unknown)
	{
		Type = Importer->GetType();
	}

	if (!HasActiveServer())
	{
		MAHO_CORE_ERROR("FResourceSystem::BeginImport: ResourceServer unavailable");
		return {};
	}

	const std::uint64_t LoadId = RequestLoadId(Config.SourcePath);
	if (LoadId == 0)
	{
		return {};
	}

	FGCSystem* GC = Detail::GetGCSystem();
	if (!GC)
	{
		MAHO_CORE_ERROR("FResourceSystem::BeginImport: GC unavailable");
		ReleaseLoadId(LoadId);
		return {};
	}

	FObjectRef ResourceRef = GC->NewObject<TResource>(
		&PackageObj,
		Config.ObjectName,
		Type,
		Config.SourcePath);
	TResource* Resource = ResourceRef.Cast<TResource>();
	if (!Resource)
	{
		MAHO_CORE_ERROR("FResourceSystem::BeginImport: NewObject failed");
		ReleaseLoadId(LoadId);
		return {};
	}

	Resource->SetLoadState(EResourceLoadState::Pending);

	if (!PackageObj.RegisterObject(Resource))
	{
		ReleaseLoadId(LoadId);
		Resource->ClearOuter();
		return {};
	}

	if (!RegisterResource(ResourceRef))
	{
		ReleaseLoadId(LoadId);
		Resource->ClearOuter();
		return {};
	}

	FPendingImport Pending;
	Pending.LoadId = LoadId;
	Pending.Resource = ResourceRef;
	Pending.Config = Config;
	Pending.Importer = Importer;
	PendingImports.emplace(LoadId, std::move(Pending));

	return ResourceRef;
}

} // namespace Maho
