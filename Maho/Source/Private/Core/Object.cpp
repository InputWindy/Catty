#include <Core/Object/Object.h>

#include <Core/System/Log.h>
#include <Core/Extension/GC.h>
#include <Core/Object/Package.h>

namespace Maho
{

namespace
{

[[nodiscard]] bool PropertyTypesCompatible(EPropertyType Expected, EPropertyType Actual)
{
	if (Expected == Actual)
	{
		return true;
	}
	switch (Expected)
	{
	case EPropertyType::Int32:
	case EPropertyType::Int64:
	case EPropertyType::UInt32:
	case EPropertyType::EnumInt32:
		return Actual == EPropertyType::Int32
			|| Actual == EPropertyType::Int64
			|| Actual == EPropertyType::UInt32
			|| Actual == EPropertyType::EnumInt32;
	case EPropertyType::UInt64:
		return Actual == EPropertyType::UInt64
			|| Actual == EPropertyType::Int64
			|| Actual == EPropertyType::Int32
			|| Actual == EPropertyType::UInt32;
	case EPropertyType::Float:
	case EPropertyType::Double:
		return Actual == EPropertyType::Float || Actual == EPropertyType::Double;
	case EPropertyType::ObjectRef:
		return Actual == EPropertyType::ObjectRef;
	default:
		return false;
	}
}

} // namespace

// --- UObject ---

UObject::UObject(UPackage* InOuter, std::string InObjectName)
	: Outer(static_cast<UObject*>(InOuter))
	, ObjectName(std::move(InObjectName))
{
}

UObject::~UObject()
{
	ClearOuter();
}

void UObject::OnPoolTearDown()
{
}

bool UObject::GetPropertyValue(std::string_view Name, FPropertyValue& OutValue) const
{
	const FProperty* Prop = GetObjectType().FindPropertyInHierarchy(Name);
	if (!Prop || !Prop->Getter)
	{
		return false;
	}
	return Prop->Getter(this, OutValue);
}

bool UObject::SetPropertyValue(std::string_view Name, const FPropertyValue& Value)
{
	const FProperty* Prop = GetObjectType().FindPropertyInHierarchy(Name);
	if (!Prop || !Prop->Setter)
	{
		return false;
	}
	return Prop->Setter(this, Value);
}

bool UObject::CallFunction(
	std::string_view Name,
	const FPropertyValue* Args,
	std::size_t ArgCount,
	FPropertyValue* OutReturn)
{
	const FFunction* Func = GetObjectType().FindFunctionInHierarchy(Name);
	if (!Func || !Func->Invoke)
	{
		return false;
	}
	if (ArgCount != Func->ParamCount)
	{
		return false;
	}
	if (Func->ParamCount > 0 && (!Args || !Func->ParamTypes))
	{
		return false;
	}
	for (std::size_t I = 0; I < Func->ParamCount; ++I)
	{
		if (!PropertyTypesCompatible(Func->ParamTypes[I], Args[I].Type))
		{
			return false;
		}
	}
	return Func->Invoke(this, Args, ArgCount, OutReturn);
}

void UObject::ClearOuter()
{
	if (!Outer)
	{
		return;
	}

	// Detach while Outer Ref still pins the package, then drop the pin.
	if (UPackage* Package = Outer.Cast<UPackage>())
	{
		Package->DetachObject(this);
	}
	Outer.Reset();
}

FObjectRef UObject::GetOuter() const
{
	return Outer;
}

FObjectRef UObject::GetPackage() const
{
	const UObject* Current = this;
	while (Current->Outer)
	{
		Current = Current->Outer.Get();
	}
	return FObjectRef::Wrap(const_cast<UObject*>(Current));
}

std::string UObject::GetPathName() const
{
	if (!Outer)
	{
		return ObjectName;
	}
	return Outer->GetName() + "." + ObjectName;
}

bool UObject::HasAnyFlags(EObjectFlags Test) const
{
	return HasAnyObjectFlags(ObjectFlags, Test);
}

bool UObject::IsPendingKill() const
{
	return HasAnyFlags(EObjectFlags::PendingKill);
}

std::uint32_t UObject::AddRef()
{
	return ++RefCount;
}

std::uint32_t UObject::ReleaseRef()
{
	if (RefCount > 0)
	{
		--RefCount;
	}
	return RefCount;
}

void UObject::GetReferencedObjects(std::vector<UObject*>& OutObjects) const
{
	(void)OutObjects;
}

void UObject::SetReferencedObjects(const std::vector<UObject*>& InObjects)
{
	(void)InObjects;
}

// --- FObjectRef ---

FObjectRef::FObjectRef(const FObjectRef& Other)
	: Object(Other.Object)
{
	if (Object)
	{
		Object->AddRef();
	}
}

FObjectRef::FObjectRef(FObjectRef&& Other) noexcept
	: Object(Other.Object)
{
	Other.Object = nullptr;
}

FObjectRef::~FObjectRef()
{
	Reset();
}

FObjectRef& FObjectRef::operator=(const FObjectRef& Other)
{
	if (this == &Other)
	{
		return *this;
	}

	Reset();
	Object = Other.Object;
	if (Object)
	{
		Object->AddRef();
	}
	return *this;
}

FObjectRef& FObjectRef::operator=(FObjectRef&& Other) noexcept
{
	if (this == &Other)
	{
		return *this;
	}

	Reset();
	Object = Other.Object;
	Other.Object = nullptr;
	return *this;
}

std::uint32_t FObjectRef::GetRefCount() const
{
	return Object ? Object->GetRefCount() : 0;
}

FObjectRef FObjectRef::Wrap(UObject* InObject)
{
	return FObjectRef(InObject);
}

void FObjectRef::Reset()
{
	if (Object)
	{
		Object->ReleaseRef();
		Object = nullptr;
	}
}

FObjectRef::FObjectRef(UObject* InObject)
	: Object(InObject)
{
	if (Object)
	{
		Object->AddRef();
	}
}

} // namespace Maho
