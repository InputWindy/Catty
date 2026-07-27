#include "Catty/Resource/Object.h"

#include "Catty/Core/Log.h"
#include "Catty/Resource/GCManager.h"
#include "Catty/Resource/Package.h"

namespace Catty
{

namespace
{

std::uint32_t AllocateWeakSerial()
{
	static std::uint32_t Next = 1;
	const std::uint32_t Serial = Next++;
	if (Next == 0)
	{
		Next = 1;
	}
	return Serial;
}

} // namespace

// --- FObject ---

FObject::FObject(FPackage* InOuter, std::string InObjectName)
	: Outer(static_cast<FObject*>(InOuter))
	, ObjectName(std::move(InObjectName))
	, WeakSerial(AllocateWeakSerial())
{
}

FObject::~FObject()
{
	ClearOuter();
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
	return Outer;
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

void FObject::AddToRoot()
{
	if (!GCOwner)
	{
		CATTY_CORE_ERROR("FObject::AddToRoot: '{}' has no GCOwner", GetPathName());
		return;
	}
	GCOwner->AddToRoot(*this);
}

void FObject::RemoveFromRoot()
{
	if (!GCOwner)
	{
		CATTY_CORE_ERROR("FObject::RemoveFromRoot: '{}' has no GCOwner", GetPathName());
		return;
	}
	GCOwner->RemoveFromRoot(*this);
}

bool FObject::IsRooted() const
{
	return GCOwner && GCOwner->IsInRootSet(*this);
}

void FObject::MarkPendingKill()
{
	if (GCOwner)
	{
		GCOwner->EnqueuePendingKill(*this);
		return;
	}

	ClearFlags(EObjectFlags::ImmediateDestroy);
	AddFlags(EObjectFlags::PendingKill);
}

void FObject::MarkForImmediateDestroy()
{
	if (GCOwner)
	{
		GCOwner->EnqueueImmediateDestroy(*this);
		return;
	}

	ClearFlags(EObjectFlags::PendingKill);
	AddFlags(EObjectFlags::ImmediateDestroy);
}

bool FObject::SplitObjectPath(
	const std::string& PathName,
	std::string& OutPackageName,
	std::string& OutObjectName)
{
	const std::size_t Dot = PathName.find_last_of('.');
	if (Dot == std::string::npos || Dot == 0 || Dot + 1 >= PathName.size())
	{
		return false;
	}

	OutPackageName = PathName.substr(0, Dot);
	OutObjectName = PathName.substr(Dot + 1);
	return !OutPackageName.empty() && !OutObjectName.empty();
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

// --- FObjectWeakRef ---

FObjectWeakRef::FObjectWeakRef(FObject* InObject)
	: Object(InObject)
	, Serial(InObject ? InObject->GetWeakSerial() : 0)
{
}

FObjectWeakRef::FObjectWeakRef(const FObjectRef& Ref)
	: Object(Ref ? &*Ref : nullptr)
	, Serial(Object ? Object->GetWeakSerial() : 0)
{
}

bool FObjectWeakRef::IsValid() const
{
	return Object != nullptr && Object->GetWeakSerial() == Serial;
}

FObjectRef FObjectWeakRef::Pin() const
{
	if (!IsValid())
	{
		return {};
	}
	return FObjectRef::Wrap(Object);
}

void FObjectWeakRef::Reset()
{
	Object = nullptr;
	Serial = 0;
}

bool FObjectWeakRef::operator==(const FObjectWeakRef& Other) const
{
	return Object == Other.Object && Serial == Other.Serial;
}

bool FObjectWeakRef::operator!=(const FObjectWeakRef& Other) const
{
	return !(*this == Other);
}

} // namespace Catty
