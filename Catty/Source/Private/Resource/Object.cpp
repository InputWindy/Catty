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

FPackageRef FObject::GetOuter() const
{
	return FPackageRef(Outer);
}

FPackageRef FObject::GetPackage() const
{
	return GetOuter();
}

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

} // namespace Catty
