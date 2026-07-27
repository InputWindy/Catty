#include "Catty/Resource/Package.h"

#include "Catty/Core/Log.h"
#include "Catty/Resource/Resource.h"

namespace Catty
{

namespace
{

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
	: Name(std::move(InName))
	, Flags(InFlags)
{
	if (!HasAnyPackageFlags(Flags, EPackageFlags::Transient)
		&& !HasAnyPackageFlags(Flags, EPackageFlags::Persistent))
	{
		Flags |= EPackageFlags::Transient;
	}
}

FPackage::~FPackage()
{
	ClearExports();
}

FObject* FPackage::FindObject(const std::string& ObjectName) const
{
	const auto It = Exports.find(ObjectName);
	if (It == Exports.end())
	{
		return nullptr;
	}
	return It->second.get();
}

std::vector<FObject*> FPackage::GetExports() const
{
	std::vector<FObject*> Result;
	Result.reserve(Exports.size());
	for (const auto& Pair : Exports)
	{
		Result.push_back(Pair.second.get());
	}
	return Result;
}

bool FPackage::RegisterExport(std::unique_ptr<FObject> Object)
{
	if (!Object)
	{
		return false;
	}

	if (Object->GetName().empty())
	{
		CATTY_CORE_ERROR("FPackage::RegisterExport: empty ObjectName");
		return false;
	}

	if (Exports.find(Object->GetName()) != Exports.end())
	{
		CATTY_CORE_ERROR(
			"FPackage::RegisterExport: '{}' already exists in package '{}'",
			Object->GetName(),
			Name);
		return false;
	}

	Object->Outer = this;
	const std::string Key = Object->GetName();
	Exports.emplace(Key, std::move(Object));
	return true;
}

std::unique_ptr<FObject> FPackage::UnregisterExport(const std::string& ObjectName)
{
	const auto It = Exports.find(ObjectName);
	if (It == Exports.end())
	{
		return {};
	}

	std::unique_ptr<FObject> Removed = std::move(It->second);
	Exports.erase(It);
	if (Removed)
	{
		Removed->Outer = nullptr;
	}
	return Removed;
}

void FPackage::ClearExports()
{
	for (auto& Pair : Exports)
	{
		if (Pair.second)
		{
			Pair.second->Outer = nullptr;
		}
	}
	Exports.clear();
}

bool FPackage::Serialize(FJsonValue& OutObject) const
{
	if (!OutObject.IsObject())
	{
		OutObject = FJsonValue::Object();
	}

	OutObject.SetField("name", FJsonValue::String(Name));
	OutObject.SetField(
		"flags",
		FJsonValue::Number(static_cast<std::int64_t>(static_cast<std::uint32_t>(Flags))));

	FJsonValue ExportArray = FJsonValue::Array();
	for (const auto& Pair : Exports)
	{
		FObject* Object = Pair.second.get();
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

		ExportArray.AddElement(Entry);
	}
	OutObject.SetField("exports", ExportArray);
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
			Name = LoadedName;
		}
	}

	if (InObject.HasField("flags"))
	{
		Flags = static_cast<EPackageFlags>(
			static_cast<std::uint32_t>(InObject.GetField("flags").AsInt64(0)));
		if (!HasAnyPackageFlags(Flags, EPackageFlags::Transient)
			&& !HasAnyPackageFlags(Flags, EPackageFlags::Persistent))
		{
			Flags |= EPackageFlags::Persistent;
		}
	}

	return true;
}

} // namespace Catty
