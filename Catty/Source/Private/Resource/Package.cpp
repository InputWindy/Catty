#include "Catty/Resource/Package.h"

#include "Catty/Core/Log.h"
#include "Catty/Resource/Resource.h"

#include <unordered_map>
#include <vector>

namespace Catty
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
	case EResourceType::Mesh: return "Mesh";
	case EResourceType::Material: return "Material";
	case EResourceType::Shader: return "Shader";
	case EResourceType::Audio: return "Audio";
	case EResourceType::Data: return "Data";
	case EResourceType::Unknown:
	default: return "Unknown";
	}
}

} // namespace

FPackage::FPackage(std::string InName, EPackageFlags InFlags)
	: FObject(nullptr, std::move(InName))
	, PackageFlags(InFlags)
{
	if (!HasAnyPackageFlags(PackageFlags, EPackageFlags::Transient)
		&& !HasAnyPackageFlags(PackageFlags, EPackageFlags::Persistent))
	{
		PackageFlags |= EPackageFlags::Transient;
	}
}

FPackage::~FPackage()
{
	if (Objects.empty())
	{
		return;
	}

	CATTY_CORE_ERROR(
		"FPackage '{}' destroyed with {} objects still registered — release object Refs first",
		GetName(),
		Objects.size());

	std::vector<FObject*> Snapshot;
	Snapshot.reserve(Objects.size());
	for (const auto& Pair : Objects)
	{
		Snapshot.push_back(Pair.second);
	}

	for (FObject* Object : Snapshot)
	{
		if (Object)
		{
			Object->ClearOuter();
		}
	}

	Objects.clear();
}

bool FPackage::IsPersistent() const
{
	return HasAnyPackageFlags(PackageFlags, EPackageFlags::Persistent);
}

bool FPackage::IsTransient() const
{
	return HasAnyPackageFlags(PackageFlags, EPackageFlags::Transient) && !IsPersistent();
}

FObjectRef FPackage::FindObject(const std::string& InObjectName) const
{
	const auto It = Objects.find(InObjectName);
	if (It == Objects.end())
	{
		return {};
	}
	return FObjectRef::Wrap(It->second);
}

bool FPackage::RegisterObject(FObject* Object)
{
	if (!Object)
	{
		return false;
	}

	if (Object->GetName().empty())
	{
		CATTY_CORE_ERROR("FPackage::RegisterObject: empty ObjectName");
		return false;
	}

	if (Objects.find(Object->GetName()) != Objects.end())
	{
		CATTY_CORE_ERROR(
			"FPackage::RegisterObject: '{}' already exists in package '{}'",
			Object->GetName(),
			GetName());
		return false;
	}

	// Object.Outer is already an FObjectRef from construction — that pin keeps us alive.
	if (Object->Outer.Get() != static_cast<FObject*>(this))
	{
		CATTY_CORE_ERROR(
			"FPackage::RegisterObject: '{}' Outer mismatch for package '{}'",
			Object->GetName(),
			GetName());
		return false;
	}

	Objects.emplace(Object->GetName(), Object);
	return true;
}

void FPackage::DetachObject(FObject* Object)
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

bool FPackage::Serialize(FJsonValue& OutObject) const
{
	if (!OutObject.IsObject())
	{
		OutObject = FJsonValue::Object();
	}

	OutObject.SetField("name", FJsonValue::String(GetName()));
	OutObject.SetField(
		"flags",
		FJsonValue::Number(static_cast<std::int64_t>(static_cast<std::uint32_t>(PackageFlags))));

	std::unordered_map<std::string, std::string> DependencyNameToFile;

	FJsonValue ObjectArray = FJsonValue::Array();
	for (const auto& Pair : Objects)
	{
		FObject* Object = Pair.second;
		if (!Object)
		{
			continue;
		}

		FJsonValue Entry = FJsonValue::Object();
		Entry.SetField("name", FJsonValue::String(Object->GetName()));

		if (const FResource* Resource = dynamic_cast<const FResource*>(Object))
		{
			Entry.SetField("class", FJsonValue::String("Resource"));
			Entry.SetField("type", FJsonValue::String(ResourceTypeToString(Resource->GetType())));
			Entry.SetField("source", FJsonValue::String(Resource->GetSourcePath()));
		}
		else
		{
			Entry.SetField("class", FJsonValue::String("Object"));
		}

		std::vector<FObject*> Referenced;
		Object->GetReferencedObjects(Referenced);
		if (!Referenced.empty())
		{
			FJsonValue RefArray = FJsonValue::Array();
			for (FObject* RefObj : Referenced)
			{
				if (!RefObj)
				{
					RefArray.AddElement(FJsonValue::String({}));
					continue;
				}

				RefArray.AddElement(FJsonValue::String(RefObj->GetPathName()));

				FObjectRef OtherOuter = RefObj->GetOuter();
				FPackage* OtherPackage = OtherOuter.Cast<FPackage>();
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

bool FPackage::Deserialize(const FJsonValue& InObject)
{
	if (!InObject.IsObject())
	{
		CATTY_CORE_ERROR("FPackage::Deserialize: root is not an object");
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

} // namespace Catty
