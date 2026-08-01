#include <Core/Object/Package.h>

#include <Core/Extension/GC/GC.h>
#include <Core/System/Log.h>
#include <Core/Extension/Resource/Resource.h>

#include "Core/Extension/Resource/ResourceCasset.h"

#include <unordered_map>
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

[[nodiscard]] const char* ResourceTypeToString(EResourceType Type)
{
	switch (Type)
	{
	case EResourceType::Raw: return "Raw";
	case EResourceType::Texture: return "Texture";
	case EResourceType::Texture2D: return "Texture2D";
	case EResourceType::Texture3D: return "Texture3D";
	case EResourceType::TextureCube: return "TextureCube";
	case EResourceType::TextureCubeArray: return "TextureCubeArray";
	case EResourceType::Texture2DArray: return "Texture2DArray";
	case EResourceType::Mesh: return "Mesh";
	case EResourceType::Material: return "Material";
	case EResourceType::Skeleton: return "Skeleton";
	case EResourceType::Animation: return "Animation";
	case EResourceType::AnimationGraph: return "AnimationGraph";
	case EResourceType::Prefab: return "Prefab";
	case EResourceType::Shader: return "Shader";
	case EResourceType::Audio: return "Audio";
	case EResourceType::Data: return "Data";
	case EResourceType::Unknown:
	default: return "Unknown";
	}
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

bool UPackage::Serialize(FJsonValue& OutObject) const
{
	if (!OutObject.IsObject())
	{
		OutObject = FJsonValue::Object();
	}

	OutObject.SetField("format", FJsonValue::String("casset"));
	OutObject.SetField("version", FJsonValue::Number(1));
	OutObject.SetField("name", FJsonValue::String(GetName()));
	OutObject.SetField(
		"flags",
		FJsonValue::Number(static_cast<std::int64_t>(static_cast<std::uint32_t>(PackageFlags))));

	std::unordered_map<std::string, std::string> DependencyNameToFile;

	FJsonValue ObjectArray = FJsonValue::Array();
	for (const auto& Pair : Objects)
	{
		UObject* Object = Pair.second;
		if (!Object)
		{
			continue;
		}

		FJsonValue Entry = FJsonValue::Object();
		Entry.SetField("name", FJsonValue::String(Object->GetName()));

		if (const UResource* Resource = dynamic_cast<const UResource*>(Object))
		{
			Entry.SetField("class", FJsonValue::String("Resource"));
			Entry.SetField("type", FJsonValue::String(ResourceTypeToString(Resource->GetType())));
			if (!ResourceCasset::WriteCpuPayload(*Resource, Entry))
			{
				MAHO_CORE_ERROR(
					"UPackage::Serialize: cpu payload failed for '{}'",
					Resource->GetPathName());
				return false;
			}
		}
		else
		{
			Entry.SetField("class", FJsonValue::String("Object"));
		}

		std::vector<UObject*> Referenced;
		Object->GetReferencedObjects(Referenced);
		if (!Referenced.empty())
		{
			FJsonValue RefArray = FJsonValue::Array();
			for (UObject* RefObj : Referenced)
			{
				if (!RefObj)
				{
					RefArray.AddElement(FJsonValue::String({}));
					continue;
				}

				RefArray.AddElement(FJsonValue::String(RefObj->GetPathName()));

				FObjectRef OtherOuter = RefObj->GetOuter();
				UPackage* OtherPackage = OtherOuter.Cast<UPackage>();
				if (!OtherPackage || OtherPackage == this)
				{
					continue;
				}

				if (!OtherPackage->IsPersistent())
				{
					continue;
				}

				DependencyNameToFile[OtherPackage->GetName()] = OtherPackage->GetFilePath();
			}
			Entry.SetField("refs", RefArray);
		}

		ObjectArray.AddElement(Entry);
	}
	OutObject.SetField("objects", ObjectArray);

	FJsonValue DepArray = FJsonValue::Array();
	for (const auto& Dep : DependencyNameToFile)
	{
		FJsonValue DepEntry = FJsonValue::Object();
		DepEntry.SetField("name", FJsonValue::String(Dep.first));
		DepEntry.SetField("file", FJsonValue::String(Dep.second));
		DepArray.AddElement(DepEntry);
	}
	OutObject.SetField("dependencies", DepArray);
	return true;
}

bool UPackage::Deserialize(const FJsonValue& InObject)
{
	if (!InObject.IsObject())
	{
		MAHO_CORE_ERROR("UPackage::Deserialize: root is not an object");
		return false;
	}

	if (InObject.HasField("name"))
	{
		const std::string LoadedName = InObject.GetField("name").AsString();
		if (!LoadedName.empty())
		{
			ObjectName = NormalizePackageName(LoadedName);
		}
	}

	if (InObject.HasField("flags"))
	{
		PackageFlags = static_cast<EPackageFlags>(
			static_cast<std::uint32_t>(InObject.GetField("flags").AsInt64(0)));
		if (!HasAnyPackageFlags(PackageFlags, EPackageFlags::Transient)
			&& !HasAnyPackageFlags(PackageFlags, EPackageFlags::Persistent))
		{
			PackageFlags |= EPackageFlags::Persistent;
		}
	}

	return true;
}

} // namespace Maho
