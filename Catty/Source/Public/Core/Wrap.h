#pragma once

/**
 * Core capability shortcuts for Lua / scripting convenience.
 * Engine / game C++ should prefer FGC / FResourceManager members via
 * Detail::GetGC() / Detail::GetResourceManager() (or GApp modules).
 */

#include <Core/Modules/GC.h>
#include <Core/Package.h>
#include <Core/Modules/Resource.h>
#include <Core/SoftObjectPath.h>

#include <cstddef>
#include <string>
#include <utility>

namespace Catty
{

// ---------------------------------------------------------------------------
// GC
// ---------------------------------------------------------------------------

template <typename TObject, typename... TArgs>
[[nodiscard]] FObjectRef NewObject(TArgs&&... Args)
{
	static_assert(std::is_base_of_v<FObject, TObject>, "TObject must derive from FObject");
	FGC* GC = Detail::GetGC();
	if (!GC)
	{
		return {};
	}
	return GC->NewObject<TObject>(std::forward<TArgs>(Args)...);
}

inline void CollectGarbage()
{
	if (FGC* GC = Detail::GetGC())
	{
		GC->CollectGarbage();
	}
}

inline void PurgePendingKill()
{
	if (FGC* GC = Detail::GetGC())
	{
		GC->PurgePendingKill();
	}
}

[[nodiscard]] inline FObjectRef FindPackage(const std::string& PackageName)
{
	FGC* GC = Detail::GetGC();
	return GC ? GC->FindPackage(PackageName) : FObjectRef{};
}

// ---------------------------------------------------------------------------
// Object query (FGC LiveObjects — authoritative)
// ---------------------------------------------------------------------------

[[nodiscard]] inline FObjectRef FindObject(const std::string& PackageName, const std::string& ObjectName)
{
	FGC* GC = Detail::GetGC();
	return GC ? GC->FindObject(PackageName, ObjectName) : FObjectRef{};
}

[[nodiscard]] inline FObjectRef FindObject(const std::string& PathName)
{
	FGC* GC = Detail::GetGC();
	return GC ? GC->FindObject(PathName) : FObjectRef{};
}

[[nodiscard]] inline FObjectRef FindResourceByPath(const std::string& VirtualPath)
{
	return FindObject(VirtualPath);
}

[[nodiscard]] inline FObjectRef Resolve(const FSoftObjectPath& SoftPath)
{
	FGC* GC = Detail::GetGC();
	if (!GC || !SoftPath.IsValid())
	{
		return {};
	}
	return GC->FindObject(SoftPath.GetPackageName(), SoftPath.GetAssetName());
}

[[nodiscard]] inline FObjectRef Resolve(const std::string& SoftPathString)
{
	FSoftObjectPath SoftPath;
	if (!SoftPath.TrySetPath(SoftPathString) || !SoftPath.IsValid())
	{
		return {};
	}
	return Resolve(SoftPath);
}

// ---------------------------------------------------------------------------
// Resource load / save (FResourceManager)
// ---------------------------------------------------------------------------

[[nodiscard]] inline FObjectRef FindObject(const FObjectRef& Package, const std::string& ObjectName)
{
	FPackage* PackagePtr = Package.Cast<FPackage>();
	if (!PackagePtr)
	{
		return {};
	}
	return FindObject(PackagePtr->GetName(), ObjectName);
}

[[nodiscard]] inline FObjectRef TryLoad(const FSoftObjectPath& SoftPath)
{
	FResourceManager* Manager = Detail::GetResourceManager();
	return Manager ? Manager->TryLoad(SoftPath) : FObjectRef{};
}

[[nodiscard]] inline FObjectRef TryLoad(const std::string& SoftPathString)
{
	FResourceManager* Manager = Detail::GetResourceManager();
	return Manager ? Manager->TryLoad(SoftPathString) : FObjectRef{};
}

[[nodiscard]] inline bool SavePackage(
	const FObjectRef& Package,
	const std::string& FilePath = {},
	bool bPretty = true,
	bool bSaveDependencies = true)
{
	FResourceManager* Manager = Detail::GetResourceManager();
	return Manager && Manager->SavePackage(Package, FilePath, bPretty, bSaveDependencies);
}

} // namespace Catty
