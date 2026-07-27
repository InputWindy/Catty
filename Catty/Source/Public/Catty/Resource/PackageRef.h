#pragma once

#include "Catty/Core/Export.h"

#include <cstdint>

namespace Catty
{

class FPackage;
class FResourceManager;

/**
 * Intrusive refcounted smart pointer for pool-allocated FPackage.
 * Catalog (Packages map) holds one Ref while the package is registered;
 * Unload drops that Ref — memory Frees when RefCount hits 0.
 */
class FPackageRef
{
public:
	FPackageRef() = default;

	FPackageRef(const FPackageRef& Other);
	FPackageRef(FPackageRef&& Other) noexcept;
	~FPackageRef();

	FPackageRef& operator=(const FPackageRef& Other);
	FPackageRef& operator=(FPackageRef&& Other) noexcept;

	[[nodiscard]] bool IsValid() const { return Package != nullptr; }
	[[nodiscard]] explicit operator bool() const { return Package != nullptr; }
	[[nodiscard]] FPackage& operator*() const { return *Package; }
	[[nodiscard]] FPackage* operator->() const { return Package; }

	[[nodiscard]] std::uint32_t GetRefCount() const;

	[[nodiscard]] bool operator==(const FPackageRef& Other) const { return Package == Other.Package; }
	[[nodiscard]] bool operator!=(const FPackageRef& Other) const { return Package != Other.Package; }

	void Reset();

private:
	friend class FPackage;
	friend class FResourceManager;
	friend class FObject;

	explicit FPackageRef(FPackage* InPackage);

	[[nodiscard]] FPackage* Get() const { return Package; }

	FPackage* Package = nullptr;
};

} // namespace Catty
