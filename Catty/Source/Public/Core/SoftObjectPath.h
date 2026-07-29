#pragma once

#include <Core/Export.h>

#include <string>

namespace Catty
{

class FObject;
class FObjectRef;

/**
 * Soft reference string (UE FSoftObjectPath lite).
 * Serialisable object address — not a live FObjectRef wrapper.
 *
 * Game code resolves via Resolve() (FGC LiveObjects) / TryLoad() (FResourceManager load).
 * Lua may use Core/Wrap.h shortcuts (Find / Resolve / TryLoad / SavePackage).
 *
 * Supported forms:
 *   /Game/Maps/Demo.Demo
 *   /Game/Maps/Demo.Demo:Subobject
 *   FResource'/Game/Maps/Demo.Demo'
 *   FResource'/Game/Maps/Demo.Demo:Subobject'
 *
 * Segments:
 *   AssetClass  — optional type name outside the quotes
 *   PackageName — long package path (/Game/...)
 *   AssetName   — object name inside the package
 *   SubPath     — subobject chain after ':' (no leading colon)
 */
class CATTY_API FSoftObjectPath
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
		std::string InAssetClass = {});

	/** Build from a live object: OuterPackage.ObjectName (no class / subpath). */
	[[nodiscard]] static FSoftObjectPath FromObject(const FObject& Object);

	/** Parse PathString; returns false and Reset() on failure. */
	[[nodiscard]] bool TrySetPath(const std::string& PathString);

	void Reset();

	[[nodiscard]] bool IsNull() const;
	/** PackageName and AssetName both non-empty (resolvable object identity). */
	[[nodiscard]] bool IsValid() const;
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

	/** PackageName.AssetName (no class, no subpath). */
	[[nodiscard]] std::string GetAssetPathString() const;

	/**
	 * Full soft path string.
	 * With class: Class'Package.Asset[:Sub]'
	 * Without:    Package.Asset[:Sub]
	 */
	[[nodiscard]] std::string ToString() const;

	/** Same as ToString() but never wraps Class'...'. */
	[[nodiscard]] std::string ToStringWithoutClass() const;

	/**
	 * Lookup among already-loaded packages only (no LoadPackage).
	 * No Resource module / miss → empty Ref.
	 */
	[[nodiscard]] FObjectRef Resolve() const;

	/**
	 * Resolve; if package missing, LoadPackage via FPaths then Resolve.
	 * Needs a live GApp Resource module.
	 */
	[[nodiscard]] FObjectRef TryLoad() const;

	[[nodiscard]] bool operator==(const FSoftObjectPath& Other) const;
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

} // namespace Catty
