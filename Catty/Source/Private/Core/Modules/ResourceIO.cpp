#include "ResourceIO.h"
#include "TextureImageCodec.h"

#include <Core/System/Log.h>

#include <filesystem>

namespace Catty
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

	if (!fs::is_regular_file(SourcePath, ErrorCode) || ErrorCode)
	{
		CATTY_CORE_ERROR("ResourceIO: source file missing or not regular '{}'", SourcePath);
		return false;
	}

	if (!bOverwrite && fs::exists(DestinationPath, ErrorCode) && !ErrorCode)
	{
		CATTY_CORE_ERROR("ResourceIO: destination exists and overwrite is disabled '{}'", DestinationPath);
		return false;
	}

	const fs::path Dest(DestinationPath);
	if (Dest.has_parent_path())
	{
		fs::create_directories(Dest.parent_path(), ErrorCode);
		if (ErrorCode)
		{
			CATTY_CORE_ERROR(
				"ResourceIO: failed to create parent dirs for '{}': {}",
				DestinationPath,
				ErrorCode.message());
			return false;
		}
	}

	fs::copy_file(
		SourcePath,
		DestinationPath,
		bOverwrite ? fs::copy_options::overwrite_existing : fs::copy_options::none,
		ErrorCode);
	if (ErrorCode)
	{
		CATTY_CORE_ERROR(
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
		CATTY_CORE_ERROR("ResourceIO: empty BulkData for '{}'", Config.SourcePath);
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
		CATTY_CORE_ERROR("ResourceIO: decode failed for '{}'", Config.SourcePath);
		return false;
	}

	if (Image.Dimension != ExpectedDimension)
	{
		CATTY_CORE_WARN(
			"ResourceIO: '{}' decoded dimension {} vs type expectation {} (name hint / TypeHint may be wrong)",
			Config.SourcePath,
			static_cast<int>(Image.Dimension),
			static_cast<int>(ExpectedDimension));
	}

	if (!TextureImageCodec::ApplyDecodedToTexture(Resource, std::move(Image)))
	{
		CATTY_CORE_ERROR("ResourceIO: ApplyDecodedToTexture failed for '{}'", Config.SourcePath);
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
	CATTY_CORE_ERROR("ResourceIO: export has neither CPU pixels nor SourcePath");
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

} // namespace Catty
