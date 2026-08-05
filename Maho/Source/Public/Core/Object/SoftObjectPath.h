#pragma once

#include <Core/Export.h>

#include <string>

namespace Maho
{

class UObject;
class FObjectRef;

/**
 * Soft reference string (UE FSoftObjectPath lite).
 * Serialisable object address — not a live FObjectRef wrapper.
 */
class FSoftObjectPath
{
public:
	FSoftObjectPath() = default;

	explicit FSoftObjectPath(const std::string& PathString)
	{
		(void)TrySetPath(PathString);
	}

	FSoftObjectPath(
		std::string InPackageName,
		std::string InAssetName,
		std::string InSubPath = {},
		std::string InAssetClass = {})
		: AssetClass(std::move(InAssetClass))
		, PackageName(std::move(InPackageName))
		, AssetName(std::move(InAssetName))
		, SubPath(std::move(InSubPath))
	{
	}

	[[nodiscard]] static FSoftObjectPath FromObject(const UObject& Object);

	[[nodiscard]] bool TrySetPath(const std::string& PathString);

	void Reset()
	{
		AssetClass.clear();
		PackageName.clear();
		AssetName.clear();
		SubPath.clear();
	}

	[[nodiscard]] bool IsNull() const
	{
		return PackageName.empty() && AssetName.empty();
	}

	[[nodiscard]] bool IsValid() const
	{
		return !PackageName.empty() && !AssetName.empty();
	}

	[[nodiscard]] bool HasSubPath() const { return !SubPath.empty(); }
	[[nodiscard]] bool HasAssetClass() const { return !AssetClass.empty(); }

	[[nodiscard]] const std::string& GetAssetClass() const { return AssetClass; }
	[[nodiscard]] const std::string& GetPackageName() const { return PackageName; }
	[[nodiscard]] const std::string& GetAssetName() const { return AssetName; }
	[[nodiscard]] const std::string& GetSubPath() const { return SubPath; }

	void SetAssetClass(std::string InClass) { AssetClass = std::move(InClass); }
	void SetPackageName(std::string InPackage) { PackageName = std::move(InPackage); }
	void SetAssetName(std::string InAsset) { AssetName = std::move(InAsset); }
	void SetSubPath(std::string InSub) { SubPath = std::move(InSub); }

	[[nodiscard]] std::string GetAssetPathString() const;
	[[nodiscard]] std::string ToString() const;
	[[nodiscard]] std::string ToStringWithoutClass() const;

	[[nodiscard]] FObjectRef Resolve() const;
	[[nodiscard]] FObjectRef TryLoad() const;

	[[nodiscard]] bool operator==(const FSoftObjectPath& Other) const
	{
		return AssetClass == Other.AssetClass
			&& PackageName == Other.PackageName
			&& AssetName == Other.AssetName
			&& SubPath == Other.SubPath;
	}

	[[nodiscard]] bool operator!=(const FSoftObjectPath& Other) const
	{
		return !(*this == Other);
	}

private:
	std::string AssetClass;
	std::string PackageName;
	std::string AssetName;
	std::string SubPath;
};

} // namespace Maho
