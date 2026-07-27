#include "Catty/Resource/Object.h"

#include "Catty/Core/Log.h"
#include "Catty/Resource/GCManager.h"
#include "Catty/Resource/Package.h"

namespace Catty
{

FObject::FObject(FPackage* InOuter, std::string InObjectName)
	: Outer(InOuter)
	, ObjectName(std::move(InObjectName))
{
}

FObject::~FObject() = default;

std::string FObject::GetPathName() const
{
	if (!Outer)
	{
		return ObjectName;
	}
	return Outer->GetName() + "." + ObjectName;
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
	if (GCOwner)
	{
		GCOwner->AddToRoot(*this);
		return;
	}
	++RootCount;
}

void FObject::RemoveFromRoot()
{
	if (GCOwner)
	{
		GCOwner->RemoveFromRoot(*this);
		return;
	}

	if (RootCount == 0)
	{
		CATTY_CORE_WARN("FObject::RemoveFromRoot: '{}' already has RootCount==0", GetPathName());
		return;
	}
	--RootCount;
}

void FObject::MarkPendingKill()
{
	ClearFlags(EObjectFlags::ImmediateDestroy);
	AddFlags(EObjectFlags::PendingKill);
}

void FObject::MarkForImmediateDestroy()
{
	ClearFlags(EObjectFlags::PendingKill);
	AddFlags(EObjectFlags::ImmediateDestroy);
}

void FObject::AddReferencedObjects(FReferenceCollector& Collector)
{
	(void)Collector;
}

FObjectRef::FObjectRef(FObject* InObject)
	: Object(InObject)
{
	if (Object)
	{
		Object->AddRef();
	}
}

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

	if (Object)
	{
		Object->ReleaseRef();
	}

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

	if (Object)
	{
		Object->ReleaseRef();
	}

	Object = Other.Object;
	Other.Object = nullptr;
	return *this;
}

void FObjectRef::Reset()
{
	if (Object)
	{
		Object->ReleaseRef();
		Object = nullptr;
	}
}

} // namespace Catty
