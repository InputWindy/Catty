#pragma once

/**
 * Core capability shortcuts for Lua / scripting convenience.
 * Engine / game C++ should prefer FGC / FResourceManager members via
 * Detail::GetGC() / Detail::GetResourceManager() (or GApp modules).
 */

#include <Core/GC.h>
#include <Core/Resource/ResourceManager.h>
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

template <typename TObject, typename TDestroyFn>
void RegisterObjectType(std::size_t InitialChunkSlots, TDestroyFn&& DestroyFn)
{
	if (FGC* GC = Detail::GetGC())
	{
		GC->RegisterObjectType<TObject>(
			InitialChunkSlots,
			std::forward<TDestroyFn>(DestroyFn));
	}
}

template <typename TObject>
[[nodiscard]] bool HasPooledType()
{
	FGC* GC = Detail::GetGC();
	return GC && GC->HasPooledType<TObject>();
}

template <typename TObject>
[[nodiscard]] std::size_t GetPooledLiveCount()
{
	FGC* GC = Detail::GetGC();
	return GC ? GC->GetPooledLiveCount<TObject>() : 0;
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

// ---------------------------------------------------------------------------
// Resource (Lua-facing; Create/Register/Unload/Flush stay on FResourceManager)
// ---------------------------------------------------------------------------

[[nodiscard]] inline FObjectRef FindObject(const FObjectRef& Package, const std::string& ObjectName)
{
	FResourceManager* Manager = Detail::GetResourceManager();
	return Manager ? Manager->FindObject(Package, ObjectName) : FObjectRef{};
}

[[nodiscard]] inline FObjectRef FindObject(const std::string& PackageName, const std::string& ObjectName)
{
	FResourceManager* Manager = Detail::GetResourceManager();
	return Manager ? Manager->FindObject(PackageName, ObjectName) : FObjectRef{};
}

[[nodiscard]] inline FObjectRef FindResourceByPath(const std::string& VirtualPath)
{
	FResourceManager* Manager = Detail::GetResourceManager();
	return Manager ? Manager->FindResourceByPath(VirtualPath) : FObjectRef{};
}

[[nodiscard]] inline FObjectRef Resolve(const FSoftObjectPath& SoftPath)
{
	FResourceManager* Manager = Detail::GetResourceManager();
	return Manager ? Manager->Resolve(SoftPath) : FObjectRef{};
}

[[nodiscard]] inline FObjectRef Resolve(const std::string& SoftPathString)
{
	FResourceManager* Manager = Detail::GetResourceManager();
	return Manager ? Manager->Resolve(SoftPathString) : FObjectRef{};
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
