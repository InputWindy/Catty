#include <Core/Object/SoftObjectPath.h>

#include <Core/Extension/GC/GC.h>
#include <Core/System/Log.h>
#include <Core/Object/Object.h>
#include <Core/System/Paths.h>
#include <Core/Extension/Resource/Resource.h>

#include <cctype>

namespace Maho
{

namespace
{

[[nodiscard]] std::string TrimAscii(std::string Text)
{
	std::size_t Begin = 0;
	while (Begin < Text.size()
		&& std::isspace(static_cast<unsigned char>(Text[Begin])))
	{
		++Begin;
	}
	std::size_t End = Text.size();
	while (End > Begin
		&& std::isspace(static_cast<unsigned char>(Text[End - 1])))
	{
		--End;
	}
	return Text.substr(Begin, End - Begin);
}

[[nodiscard]] bool IsSimpleIdentifier(const std::string& Text)
{
	if (Text.empty())
	{
		return false;
	}
	for (const char Ch : Text)
	{
		const unsigned char U = static_cast<unsigned char>(Ch);
		if (!(std::isalnum(U) || Ch == '_'))
		{
			return false;
		}
	}
	return true;
}

/**
 * Split "Package.Asset[:Sub...]" into package / asset / subpath.
 * Package is everything before the last '.' that precedes any ':'.
 */
[[nodiscard]] bool SplitAssetPathBody(
	const std::string& Body,
	std::string& OutPackage,
	std::string& OutAsset,
	std::string& OutSub)
{
	OutPackage.clear();
	OutAsset.clear();
	OutSub.clear();

	if (Body.empty())
	{
		return false;
	}

	std::string PathPart = Body;
	const std::size_t Colon = Body.find(':');
	if (Colon != std::string::npos)
	{
		PathPart = Body.substr(0, Colon);
		OutSub = Body.substr(Colon + 1);
		if (OutSub.empty())
		{
			return false;
		}
	}

	PathPart = FPaths::NormalizePackagePath(PathPart);
	if (PathPart.empty())
	{
		return false;
	}

	const std::size_t Dot = PathPart.find_last_of('.');
	if (Dot == std::string::npos || Dot == 0 || Dot + 1 >= PathPart.size())
	{
		// Package-only path — allowed as partial soft path.
		OutPackage = PathPart;
		return true;
	}

	OutPackage = PathPart.substr(0, Dot);
	OutAsset = PathPart.substr(Dot + 1);
	return !OutPackage.empty() && !OutAsset.empty();
}

} // namespace

FSoftObjectPath::FSoftObjectPath(
	std::string InPackageName,
	std::string InAssetName,
	std::string InSubPath,
	std::string InAssetClass)
	: AssetClass(std::move(InAssetClass))
	, PackageName(FPaths::NormalizePackagePath(std::move(InPackageName)))
	, AssetName(std::move(InAssetName))
	, SubPath(std::move(InSubPath))
{
}

FSoftObjectPath FSoftObjectPath::FromObject(const UObject& Object)
{
	FSoftObjectPath Path;
	const FObjectRef Package = Object.GetPackage();
	if (!Package || &*Package == &Object)
	{
		// Package root (or orphan with no outer) — package-only path.
		Path.PackageName = FPaths::NormalizePackagePath(Object.GetName());
		return Path;
	}

	Path.PackageName = FPaths::NormalizePackagePath(Package->GetName());
	Path.AssetName = Object.GetName();
	return Path;
}

bool FSoftObjectPath::TrySetPath(const std::string& PathString)
{
	Reset();

	const std::string Trimmed = TrimAscii(PathString);
	if (Trimmed.empty())
	{
		return false;
	}

	std::string ClassName;
	std::string Body = Trimmed;

	const std::size_t FirstQuote = Trimmed.find('\'');
	if (FirstQuote != std::string::npos)
	{
		const std::size_t SecondQuote = Trimmed.find('\'', FirstQuote + 1);
		if (SecondQuote == std::string::npos || SecondQuote <= FirstQuote + 1)
		{
			return false;
		}
		// Trailing junk after closing quote is invalid.
		if (SecondQuote + 1 != Trimmed.size())
		{
			return false;
		}

		ClassName = TrimAscii(Trimmed.substr(0, FirstQuote));
		Body = Trimmed.substr(FirstQuote + 1, SecondQuote - FirstQuote - 1);
		if (!ClassName.empty() && !IsSimpleIdentifier(ClassName))
		{
			return false;
		}
	}

	std::string Package;
	std::string Asset;
	std::string Sub;
	if (!SplitAssetPathBody(Body, Package, Asset, Sub))
	{
		return false;
	}

	AssetClass = std::move(ClassName);
	PackageName = std::move(Package);
	AssetName = std::move(Asset);
	SubPath = std::move(Sub);
	return true;
}

void FSoftObjectPath::Reset()
{
	AssetClass.clear();
	PackageName.clear();
	AssetName.clear();
	SubPath.clear();
}

bool FSoftObjectPath::IsNull() const
{
	return AssetClass.empty()
		&& PackageName.empty()
		&& AssetName.empty()
		&& SubPath.empty();
}

bool FSoftObjectPath::IsValid() const
{
	return !PackageName.empty() && !AssetName.empty();
}

std::string FSoftObjectPath::GetAssetPathString() const
{
	if (PackageName.empty())
	{
		return {};
	}
	if (AssetName.empty())
	{
		return PackageName;
	}
	return PackageName + "." + AssetName;
}

std::string FSoftObjectPath::ToStringWithoutClass() const
{
	std::string Result = GetAssetPathString();
	if (Result.empty())
	{
		return {};
	}
	if (!SubPath.empty())
	{
		Result += ":";
		Result += SubPath;
	}
	return Result;
}

std::string FSoftObjectPath::ToString() const
{
	const std::string Inner = ToStringWithoutClass();
	if (Inner.empty())
	{
		return {};
	}
	if (AssetClass.empty())
	{
		return Inner;
	}
	return AssetClass + "'" + Inner + "'";
}

bool FSoftObjectPath::operator==(const FSoftObjectPath& Other) const
{
	return AssetClass == Other.AssetClass
		&& PackageName == Other.PackageName
		&& AssetName == Other.AssetName
		&& SubPath == Other.SubPath;
}

FObjectRef FSoftObjectPath::Resolve() const
{
	FGCSystem* GC = Detail::GetGCSystem();
	if (!GC || !IsValid())
	{
		return {};
	}
	if (HasSubPath())
	{
		MAHO_CORE_WARN(
			"FSoftObjectPath::Resolve: subobject path not implemented yet ('{}') — resolving asset only",
			ToStringWithoutClass());
	}
	return GC->FindObject(GetPackageName(), GetAssetName());
}

FObjectRef FSoftObjectPath::TryLoad() const
{
	FResourceSystem* Manager = Detail::GetResourceSystem();
	return Manager ? Manager->TryLoad(*this) : FObjectRef{};
}

} // namespace Maho
