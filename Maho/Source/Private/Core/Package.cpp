#include <Core/Object/Package.h>

#include <Core/Extension/GC/GC.h>
#include <Core/System/Log.h>

#include <vector>

namespace Maho
{

namespace
{

[[nodiscard]] std::string NormalizePackageName(std::string Name)
{
	for (char& Ch : Name)
	{
		if (Ch == '\\')
		{
			Ch = '/';
		}
	}
	while (Name.size() >= 2 && Name[0] == '.' && Name[1] == '/')
	{
		Name.erase(0, 2);
	}
	return Name;
}

} // namespace

UPackage::UPackage(std::string InName, EPackageFlags InFlags)
	: UObject(nullptr, std::move(InName))
	, PackageFlags(InFlags)
{
	if (!HasAnyPackageFlags(PackageFlags, EPackageFlags::Transient)
		&& !HasAnyPackageFlags(PackageFlags, EPackageFlags::Persistent))
	{
		PackageFlags |= EPackageFlags::Transient;
	}
}

UPackage::~UPackage()
{
	if (Objects.empty())
	{
		return;
	}

	MAHO_CORE_ERROR(
		"UPackage '{}' destroyed with {} objects still registered — release object Refs first",
		GetName(),
		Objects.size());

	std::vector<UObject*> Snapshot;
	Snapshot.reserve(Objects.size());
	for (const auto& Pair : Objects)
	{
		Snapshot.push_back(Pair.second);
	}

	for (UObject* Object : Snapshot)
	{
		if (Object)
		{
			Object->ClearOuter();
		}
	}

	Objects.clear();
}

void UPackage::OnPoolTearDown()
{
	if (Objects.empty())
	{
		return;
	}

	// Package should only reach TearDown after Outer pins are gone.
	// Leftover name-table entries are stale — detach and clear; do not force-free.
	MAHO_CORE_ERROR(
		"UPackage::OnPoolTearDown: '{}' still has {} object(s) — clearing name table",
		GetName(),
		GetObjectCount());

	std::vector<UObject*> Snapshot;
	Snapshot.reserve(Objects.size());
	for (const auto& Pair : Objects)
	{
		Snapshot.push_back(Pair.second);
	}

	for (UObject* Object : Snapshot)
	{
		if (Object)
		{
			Object->ClearOuter();
		}
	}

	Objects.clear();
}

bool UPackage::IsPersistent() const
{
	return HasAnyPackageFlags(PackageFlags, EPackageFlags::Persistent);
}

bool UPackage::IsTransient() const
{
	return HasAnyPackageFlags(PackageFlags, EPackageFlags::Transient) && !IsPersistent();
}

FObjectRef UPackage::FindObject(const std::string& InObjectName) const
{
	const auto It = Objects.find(InObjectName);
	if (It == Objects.end() || !It->second)
	{
		return {};
	}
	FGCSystem* GCSystem = Detail::GetGCSystem();
	if (GCSystem && GCSystem->IsInitialized() && !GCSystem->ContainsLiveObject(It->second))
	{
		return {};
	}
	return FObjectRef::Wrap(It->second);
}

bool UPackage::RegisterObject(UObject* Object)
{
	if (!Object)
	{
		return false;
	}

	if (Object->GetName().empty())
	{
		MAHO_CORE_ERROR("UPackage::RegisterObject: empty ObjectName");
		return false;
	}

	if (Objects.find(Object->GetName()) != Objects.end())
	{
		MAHO_CORE_ERROR(
			"UPackage::RegisterObject: '{}' already exists in package '{}'",
			Object->GetName(),
			GetName());
		return false;
	}

	// Object.Outer is already an FObjectRef from construction — that pin keeps us alive.
	if (Object->Outer.Get() != static_cast<UObject*>(this))
	{
		MAHO_CORE_ERROR(
			"UPackage::RegisterObject: '{}' Outer mismatch for package '{}'",
			Object->GetName(),
			GetName());
		return false;
	}

	Objects.emplace(Object->GetName(), Object);
	return true;
}

void UPackage::DetachObject(UObject* Object)
{
	if (!Object)
	{
		return;
	}

	const auto It = Objects.find(Object->GetName());
	if (It == Objects.end() || It->second != Object)
	{
		return;
	}

	Objects.erase(It);
}

} // namespace Maho
