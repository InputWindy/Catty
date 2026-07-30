#include "ResourceIO.h"

#include <Core/Log.h>

#include <filesystem>

namespace Catty
{
namespace
{
[[nodiscard]] std::string GetExtensionLower(const std::string& Path)
{
	const std::size_t Dot = Path.find_last_of('.');
	if (Dot == std::string::npos || Dot + 1 >= Path.size())
	{
		return {};
	}

	std::string Ext = Path.substr(Dot + 1);
	for (char& Ch : Ext)
	{
		if (Ch >= 'A' && Ch <= 'Z')
		{
			Ch = static_cast<char>(Ch - 'A' + 'a');
		}
	}
	return Ext;
}

[[nodiscard]] bool CopyFileToDestination(
	const std::string& SourcePath,
	const std::string& DestinationPath,
	bool bOverwrite)
{
	namespace fs = std::filesystem;
	std::error_code ErrorCode;

	if (!fs::is_regular_file(SourcePath, ErrorCode) || ErrorCode)
	{
		CATTY_CORE_ERROR(
			"ResourceIO: source file missing or not regular '{}'",
			SourcePath);
		return false;
	}

	if (!bOverwrite && fs::exists(DestinationPath, ErrorCode) && !ErrorCode)
	{
		CATTY_CORE_ERROR(
			"ResourceIO: destination exists and overwrite is disabled '{}'",
			DestinationPath);
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
} // namespace

bool TResourceIOTraits<UResource>::ImportSource(
	FResourceImportConfig& Config,
	FResourceBulkData& Bulk,
	UResource& Resource)
{
	(void)Config;
	(void)Resource;
	// Raw resource: BulkData is the payload; typed decode not required.
	return !Bulk.SourcePath.empty() || !Bulk.Bytes.empty();
}

bool TResourceIOTraits<UResource>::ExportSource(
	FResourceExportConfig& Config,
	const UResource& Resource)
{
	return CopyFileToDestination(
		Resource.GetSourcePath(),
		Config.DestinationPath,
		Config.bOverwrite);
}

bool TResourceIOTraits<UTextureResource>::MatchesSourcePath(const std::string& SourcePath)
{
	const std::string Ext = GetExtensionLower(SourcePath);
	return Ext == "png" || Ext == "jpg" || Ext == "jpeg" || Ext == "tga" || Ext == "bmp"
		|| Ext == "ktx" || Ext == "ktx2" || Ext == "dds";
}

bool TResourceIOTraits<UTextureResource>::ImportSource(
	FResourceImportConfig& Config,
	FResourceBulkData& Bulk,
	UTextureResource& Resource)
{
	(void)Resource;

	if (!MatchesSourcePath(Config.SourcePath))
	{
		CATTY_CORE_WARN(
			"TResourceIOTraits<UTextureResource>: extension not recognized as texture '{}'",
			Config.SourcePath);
	}

	if (Bulk.Bytes.empty())
	{
		CATTY_CORE_ERROR(
			"TResourceIOTraits<UTextureResource>: empty BulkData for '{}'",
			Config.SourcePath);
		return false;
	}

	// Pixel decode / mip build lands here (consume Bulk.Bytes).
	return true;
}

bool TResourceIOTraits<UTextureResource>::ExportSource(
	FResourceExportConfig& Config,
	const UTextureResource& Resource)
{
	return CopyFileToDestination(
		Resource.GetSourcePath(),
		Config.DestinationPath,
		Config.bOverwrite);
}

} // namespace Catty
