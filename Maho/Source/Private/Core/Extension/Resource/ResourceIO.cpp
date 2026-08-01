#include "ResourceIO.h"
#include "TextureImageCodec.h"
#include "MeshModelCodec.h"

#include <Core/System/Log.h>
#include <Core/System/Utf8Path.h>

#include <filesystem>

namespace Maho
{
namespace
{

[[nodiscard]] bool CopyFileToDestination(
	const std::string& SourcePath,
	const std::string& DestinationPath,
	bool bOverwrite)
{
	namespace fs = std::filesystem;
	std::error_code ErrorCode;
	const fs::path Source = PathFromUtf8(SourcePath);
	const fs::path Dest = PathFromUtf8(DestinationPath);

	if (!fs::is_regular_file(Source, ErrorCode) || ErrorCode)
	{
		MAHO_CORE_ERROR("ResourceIO: source file missing or not regular '{}'", SourcePath);
		return false;
	}

	if (!bOverwrite && fs::exists(Dest, ErrorCode) && !ErrorCode)
	{
		MAHO_CORE_ERROR("ResourceIO: destination exists and overwrite is disabled '{}'", DestinationPath);
		return false;
	}

	if (Dest.has_parent_path())
	{
		fs::create_directories(Dest.parent_path(), ErrorCode);
		if (ErrorCode)
		{
			MAHO_CORE_ERROR(
				"ResourceIO: failed to create parent dirs for '{}': {}",
				DestinationPath,
				ErrorCode.message());
			return false;
		}
	}

	fs::copy_file(
		Source,
		Dest,
		bOverwrite ? fs::copy_options::overwrite_existing : fs::copy_options::none,
		ErrorCode);
	if (ErrorCode)
	{
		MAHO_CORE_ERROR(
			"ResourceIO: copy '{}' → '{}' failed: {}",
			SourcePath,
			DestinationPath,
			ErrorCode.message());
		return false;
	}
	return true;
}

template <typename TTexture>
[[nodiscard]] bool ImportTextureCpu(
	FResourceImportConfig& Config,
	FResourceBulkData& Bulk,
	TTexture& Resource)
{
	if (Bulk.Bytes.empty())
	{
		MAHO_CORE_ERROR("ResourceIO: empty BulkData for '{}'", Config.SourcePath);
		return false;
	}

	const ETextureDimension ExpectedDimension = Resource.GetDimension();

	FDecodedImage Image;
	if (!TextureImageCodec::DecodeFromMemory(
			Bulk.Bytes.data(),
			Bulk.Bytes.size(),
			Config.SourcePath,
			Image))
	{
		MAHO_CORE_ERROR("ResourceIO: decode failed for '{}'", Config.SourcePath);
		return false;
	}

	if (Image.Dimension != ExpectedDimension)
	{
		MAHO_CORE_WARN(
			"ResourceIO: '{}' decoded dimension {} vs type expectation {} (name hint / TypeHint may be wrong)",
			Config.SourcePath,
			static_cast<int>(Image.Dimension),
			static_cast<int>(ExpectedDimension));
	}

	if (!TextureImageCodec::ApplyDecodedToTexture(Resource, std::move(Image)))
	{
		MAHO_CORE_ERROR("ResourceIO: ApplyDecodedToTexture failed for '{}'", Config.SourcePath);
		return false;
	}
	return true;
}

template <typename TTexture>
[[nodiscard]] bool ExportTextureCpu(FResourceExportConfig& Config, const TTexture& Resource)
{
	if (!Resource.GetPixels().empty())
	{
		return TextureImageCodec::EncodeToFile(Resource, Config.DestinationPath, Config.bOverwrite);
	}
	if (!Resource.GetSourcePath().empty())
	{
		return CopyFileToDestination(Resource.GetSourcePath(), Config.DestinationPath, Config.bOverwrite);
	}
	MAHO_CORE_ERROR("ResourceIO: export has neither CPU pixels nor SourcePath");
	return false;
}

} // namespace

bool TResourceIOTraits<UResource>::ImportSource(
	FResourceImportConfig& Config,
	FResourceBulkData& Bulk,
	UResource& Resource)
{
	(void)Config;
	(void)Resource;
	return !Bulk.SourcePath.empty() || !Bulk.Bytes.empty();
}

bool TResourceIOTraits<UResource>::ExportSource(
	FResourceExportConfig& Config,
	const UResource& Resource)
{
	return CopyFileToDestination(Resource.GetSourcePath(), Config.DestinationPath, Config.bOverwrite);
}

bool TResourceIOTraits<UTexture2D>::MatchesSourcePath(const std::string& SourcePath)
{
	const std::string Ext = TextureImageCodec::GetExtensionLower(SourcePath);
	if (TextureImageCodec::PathLooksLikeCube(SourcePath)
		|| TextureImageCodec::PathLooksLikeCubeArray(SourcePath)
		|| TextureImageCodec::PathLooksLikeTexture3D(SourcePath)
		|| TextureImageCodec::PathLooksLikeTexture2DArray(SourcePath))
	{
		return false;
	}
	return TextureImageCodec::IsRasterExtension(Ext) || TextureImageCodec::IsKtx2Extension(Ext);
}

bool TResourceIOTraits<UTexture2D>::ImportSource(
	FResourceImportConfig& Config,
	FResourceBulkData& Bulk,
	UTexture2D& Resource)
{
	return ImportTextureCpu(Config, Bulk, Resource);
}

bool TResourceIOTraits<UTexture2D>::ExportSource(
	FResourceExportConfig& Config,
	const UTexture2D& Resource)
{
	return ExportTextureCpu(Config, Resource);
}

bool TResourceIOTraits<UTexture3D>::MatchesSourcePath(const std::string& SourcePath)
{
	return TextureImageCodec::PathLooksLikeTexture3D(SourcePath);
}

bool TResourceIOTraits<UTexture3D>::ImportSource(
	FResourceImportConfig& Config,
	FResourceBulkData& Bulk,
	UTexture3D& Resource)
{
	return ImportTextureCpu(Config, Bulk, Resource);
}

bool TResourceIOTraits<UTexture3D>::ExportSource(
	FResourceExportConfig& Config,
	const UTexture3D& Resource)
{
	return ExportTextureCpu(Config, Resource);
}

bool TResourceIOTraits<UTextureCube>::MatchesSourcePath(const std::string& SourcePath)
{
	return TextureImageCodec::PathLooksLikeCube(SourcePath);
}

bool TResourceIOTraits<UTextureCube>::ImportSource(
	FResourceImportConfig& Config,
	FResourceBulkData& Bulk,
	UTextureCube& Resource)
{
	return ImportTextureCpu(Config, Bulk, Resource);
}

bool TResourceIOTraits<UTextureCube>::ExportSource(
	FResourceExportConfig& Config,
	const UTextureCube& Resource)
{
	return ExportTextureCpu(Config, Resource);
}

bool TResourceIOTraits<UTextureCubeArray>::MatchesSourcePath(const std::string& SourcePath)
{
	return TextureImageCodec::PathLooksLikeCubeArray(SourcePath);
}

bool TResourceIOTraits<UTextureCubeArray>::ImportSource(
	FResourceImportConfig& Config,
	FResourceBulkData& Bulk,
	UTextureCubeArray& Resource)
{
	return ImportTextureCpu(Config, Bulk, Resource);
}

bool TResourceIOTraits<UTextureCubeArray>::ExportSource(
	FResourceExportConfig& Config,
	const UTextureCubeArray& Resource)
{
	return ExportTextureCpu(Config, Resource);
}

bool TResourceIOTraits<UTexture2DArray>::MatchesSourcePath(const std::string& SourcePath)
{
	return TextureImageCodec::PathLooksLikeTexture2DArray(SourcePath);
}

bool TResourceIOTraits<UTexture2DArray>::ImportSource(
	FResourceImportConfig& Config,
	FResourceBulkData& Bulk,
	UTexture2DArray& Resource)
{
	return ImportTextureCpu(Config, Bulk, Resource);
}

bool TResourceIOTraits<UTexture2DArray>::ExportSource(
	FResourceExportConfig& Config,
	const UTexture2DArray& Resource)
{
	return ExportTextureCpu(Config, Resource);
}

bool TResourceIOTraits<UPrefab>::MatchesSourcePath(const std::string& SourcePath)
{
	return MeshModelCodec::MatchesModelSourcePath(SourcePath);
}

bool TResourceIOTraits<UPrefab>::ImportSource(
	FResourceImportConfig& Config,
	FResourceBulkData& Bulk,
	UPrefab& Resource)
{
	if (Bulk.Bytes.empty())
	{
		MAHO_CORE_ERROR("ResourceIO: empty BulkData for model '{}'", Config.SourcePath);
		return false;
	}

	FResourceSystem* Resources = Detail::GetResourceSystem();
	FGCSystem* GC = Detail::GetGCSystem();
	UPackage* Package = Config.Package.Cast<UPackage>();
	if (!Resources || !GC || !Package)
	{
		MAHO_CORE_ERROR(
			"ResourceIO: Prefab import needs ResourceSystem + GC + Package for '{}'",
			Config.SourcePath);
		return false;
	}

	FDecodedModelScene Scene;
	if (!MeshModelCodec::DecodeFromMemory(
			Bulk.Bytes.data(),
			Bulk.Bytes.size(),
			Config.SourcePath,
			Scene))
	{
		MAHO_CORE_ERROR("ResourceIO: model decode failed for '{}'", Config.SourcePath);
		return false;
	}

	if (!MeshModelCodec::ApplyDecodedModelScene(
			std::move(Scene),
			*Resources,
			*GC,
			*Package,
			Resource))
	{
		MAHO_CORE_ERROR("ResourceIO: model Apply failed for '{}'", Config.SourcePath);
		return false;
	}
	return true;
}

bool TResourceIOTraits<UPrefab>::ExportSource(
	FResourceExportConfig& Config,
	const UPrefab& Resource)
{
	(void)Config;
	(void)Resource;
	MAHO_CORE_ERROR("ResourceIO: UPrefab model export is not implemented (Phase 1)");
	return false;
}

namespace
{

template <typename TResource>
[[nodiscard]] bool RejectDirectImport(FResourceImportConfig& Config, TResource&)
{
	MAHO_CORE_ERROR(
		"ResourceIO: '{}' is created by UPrefab scene Apply — not a direct file importer",
		Config.SourcePath);
	return false;
}

template <typename TResource>
[[nodiscard]] bool RejectDirectExport(FResourceExportConfig&, const TResource&)
{
	MAHO_CORE_ERROR("ResourceIO: direct export not implemented for this type (Phase 1)");
	return false;
}

} // namespace

bool TResourceIOTraits<UMaterial>::MatchesSourcePath(const std::string& SourcePath)
{
	(void)SourcePath;
	return false;
}

bool TResourceIOTraits<UMaterial>::ImportSource(
	FResourceImportConfig& Config,
	FResourceBulkData& Bulk,
	UMaterial& Resource)
{
	(void)Bulk;
	return RejectDirectImport(Config, Resource);
}

bool TResourceIOTraits<UMaterial>::ExportSource(
	FResourceExportConfig& Config,
	const UMaterial& Resource)
{
	return RejectDirectExport(Config, Resource);
}

bool TResourceIOTraits<UStaticMesh>::MatchesSourcePath(const std::string& SourcePath)
{
	(void)SourcePath;
	return false;
}

bool TResourceIOTraits<UStaticMesh>::ImportSource(
	FResourceImportConfig& Config,
	FResourceBulkData& Bulk,
	UStaticMesh& Resource)
{
	(void)Bulk;
	return RejectDirectImport(Config, Resource);
}

bool TResourceIOTraits<UStaticMesh>::ExportSource(
	FResourceExportConfig& Config,
	const UStaticMesh& Resource)
{
	return RejectDirectExport(Config, Resource);
}

bool TResourceIOTraits<USkeleton>::MatchesSourcePath(const std::string& SourcePath)
{
	(void)SourcePath;
	return false;
}

bool TResourceIOTraits<USkeleton>::ImportSource(
	FResourceImportConfig& Config,
	FResourceBulkData& Bulk,
	USkeleton& Resource)
{
	(void)Bulk;
	return RejectDirectImport(Config, Resource);
}

bool TResourceIOTraits<USkeleton>::ExportSource(
	FResourceExportConfig& Config,
	const USkeleton& Resource)
{
	return RejectDirectExport(Config, Resource);
}

bool TResourceIOTraits<UAnimation>::MatchesSourcePath(const std::string& SourcePath)
{
	(void)SourcePath;
	return false;
}

bool TResourceIOTraits<UAnimation>::ImportSource(
	FResourceImportConfig& Config,
	FResourceBulkData& Bulk,
	UAnimation& Resource)
{
	(void)Bulk;
	return RejectDirectImport(Config, Resource);
}

bool TResourceIOTraits<UAnimation>::ExportSource(
	FResourceExportConfig& Config,
	const UAnimation& Resource)
{
	return RejectDirectExport(Config, Resource);
}

bool TResourceIOTraits<UAnimationGraph>::MatchesSourcePath(const std::string& SourcePath)
{
	(void)SourcePath;
	return false;
}

bool TResourceIOTraits<UAnimationGraph>::ImportSource(
	FResourceImportConfig& Config,
	FResourceBulkData& Bulk,
	UAnimationGraph& Resource)
{
	(void)Bulk;
	return RejectDirectImport(Config, Resource);
}

bool TResourceIOTraits<UAnimationGraph>::ExportSource(
	FResourceExportConfig& Config,
	const UAnimationGraph& Resource)
{
	return RejectDirectExport(Config, Resource);
}

} // namespace Maho
