#include "Catty/Resource/Package.h"

#include "Catty/Core/Log.h"
#include "Catty/Resource/Resource.h"
#include "Catty/Resource/ResourceManager.h"

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
	if (!Objects.empty())
	{
		CATTY_CORE_ERROR(
			"FPackage '{}' destroyed with {} objects still registered — Manager must Free to pool first",
			Name,
			Objects.size());
		ClearObjects();
	}
}

std::uint32_t FPackage::AddRef()
{
	return ++RefCount;
}

std::uint32_t FPackage::ReleaseRef()
{
	if (RefCount == 0)
	{
		return 0;
	}

	--RefCount;
	if (RefCount == 0)
	{
		OnRefCountZero();
	}
	return RefCount;
}

void FPackage::OnRefCountZero()
{
	if (!Owner)
	{
		CATTY_CORE_ERROR("FPackage::OnRefCountZero: '{}' has no Owner — leaked pool slot", Name);
		return;
	}

	Owner->FreePackageMemory(this);
}

FPackageRef::FPackageRef(FPackage* InPackage)
	: Package(InPackage)
{
	if (Package)
	{
		Package->AddRef();
	}
}

FPackageRef::FPackageRef(const FPackageRef& Other)
	: Package(Other.Package)
{
	if (Package)
	{
		Package->AddRef();
	}
}

FPackageRef::FPackageRef(FPackageRef&& Other) noexcept
	: Package(Other.Package)
{
	Other.Package = nullptr;
}

FPackageRef::~FPackageRef()
{
	Reset();
}

FPackageRef& FPackageRef::operator=(const FPackageRef& Other)
{
	if (this == &Other)
	{
		return *this;
	}

	Reset();
	Package = Other.Package;
	if (Package)
	{
		Package->AddRef();
	}
	return *this;
}

FPackageRef& FPackageRef::operator=(FPackageRef&& Other) noexcept
{
	if (this == &Other)
	{
		return *this;
	}

	Reset();
	Package = Other.Package;
	Other.Package = nullptr;
	return *this;
}

std::uint32_t FPackageRef::GetRefCount() const
{
	return Package ? Package->GetRefCount() : 0;
}

void FPackageRef::Reset()
{
	if (Package)
	{
		FPackage* Dying = Package;
		Package = nullptr;
		Dying->ReleaseRef();
	}
}

FObjectRef FPackage::FindObject(const std::string& ObjectName) const
{
	const auto It = Objects.find(ObjectName);
	if (It == Objects.end())
	{
		return {};
	}
	return FObjectRef(It->second);
}

std::vector<FObjectRef> FPackage::GetObjects() const
{
	std::vector<FObjectRef> Result;
	Result.reserve(Objects.size());
	for (const auto& Pair : Objects)
	{
		Result.push_back(FObjectRef(Pair.second));
	}
	return Result;
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
			Name);
		return false;
	}

	Object->Outer = this;
	Objects.emplace(Object->GetName(), Object);
	return true;
}

FObject* FPackage::UnregisterObject(const std::string& ObjectName)
{
	const auto It = Objects.find(ObjectName);
	if (It == Objects.end())
	{
		return nullptr;
	}

	FObject* Removed = It->second;
	Objects.erase(It);
	if (Removed)
	{
		Removed->Outer = nullptr;
	}
	return Removed;
}

void FPackage::ClearObjects()
{
	for (auto& Pair : Objects)
	{
		if (Pair.second)
		{
			Pair.second->Outer = nullptr;
		}
	}
	Objects.clear();
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

		ObjectArray.AddElement(Entry);
	}
	OutObject.SetField("objects", ObjectArray);
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
