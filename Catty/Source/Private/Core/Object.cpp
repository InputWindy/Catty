#include <Core/Object.h>

#include <Core/Log.h>
#include <Core/Modules/GC.h>
#include <Core/Package.h>

namespace Catty
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

// --- FObject ---

FObject::FObject(FPackage* InOuter, std::string InObjectName)
	: Outer(static_cast<FObject*>(InOuter))
	, ObjectName(std::move(InObjectName))
{
}

FObject::~FObject()
{
	ClearOuter();
}

bool FObject::GetPropertyValue(std::string_view Name, FPropertyValue& OutValue) const
{
	const FProperty* Prop = GetObjectType().FindPropertyInHierarchy(Name);
	if (!Prop || !Prop->Getter)
	{
		return false;
	}
	return Prop->Getter(this, OutValue);
}

bool FObject::SetPropertyValue(std::string_view Name, const FPropertyValue& Value)
{
	const FProperty* Prop = GetObjectType().FindPropertyInHierarchy(Name);
	if (!Prop || !Prop->Setter)
	{
		return false;
	}
	return Prop->Setter(this, Value);
}

bool FObject::CallFunction(
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

void FObject::ClearOuter()
{
	if (!Outer)
	{
		return;
	}

	// Detach while Outer Ref still pins the package, then drop the pin.
	if (FPackage* Package = Outer.Cast<FPackage>())
	{
		Package->DetachObject(this);
	}
	Outer.Reset();
}

FObjectRef FObject::GetOuter() const
{
	return Outer;
}

FObjectRef FObject::GetPackage() const
{
	const FObject* Current = this;
	while (Current->Outer)
	{
		Current = Current->Outer.Get();
	}
	return FObjectRef::Wrap(const_cast<FObject*>(Current));
}

std::string FObject::GetPathName() const
{
	if (!Outer)
	{
		return ObjectName;
	}
	return Outer->GetName() + "." + ObjectName;
}

bool FObject::HasAnyFlags(EObjectFlags Test) const
{
	return HasAnyObjectFlags(ObjectFlags, Test);
}

bool FObject::IsPendingKill() const
{
	return HasAnyFlags(EObjectFlags::PendingKill);
}

std::uint32_t FObject::AddRef()
{
	return ++RefCount;
}

std::uint32_t FObject::ReleaseRef()
{
	if (RefCount > 0)
	{
		--RefCount;
	}
	return RefCount;
}

void FObject::GetReferencedObjects(std::vector<FObject*>& OutObjects) const
{
	(void)OutObjects;
}

void FObject::SetReferencedObjects(const std::vector<FObject*>& InObjects)
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

FObjectRef FObjectRef::Wrap(FObject* InObject)
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

FObjectRef::FObjectRef(FObject* InObject)
	: Object(InObject)
{
	if (Object)
	{
		Object->AddRef();
	}
}

} // namespace Catty
