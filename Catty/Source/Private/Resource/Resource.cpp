#include "Catty/Resource/Resource.h"

namespace Catty
{

FResource::FResource(
	FPackage* InOuter,
	std::string InObjectName,
	FResourceId InId,
	EResourceType InType,
	std::string InSourcePath)
	: FObject(InOuter, std::move(InObjectName))
	, Id(InId)
	, Type(InType)
	, SourcePath(std::move(InSourcePath))
{
}

FResource::~FResource() = default;

FResourceRef::FResourceRef(FResource* InResource)
	: Resource(InResource)
{
	if (Resource)
	{
		Resource->AddRef();
	}
}

FResourceRef::FResourceRef(const FResourceRef& Other)
	: Resource(Other.Resource)
{
	if (Resource)
	{
		Resource->AddRef();
	}
}

FResourceRef::FResourceRef(FResourceRef&& Other) noexcept
	: Resource(Other.Resource)
{
	Other.Resource = nullptr;
}

FResourceRef::~FResourceRef()
{
	Reset();
}

FResourceRef& FResourceRef::operator=(const FResourceRef& Other)
{
	if (this == &Other)
	{
		return *this;
	}

	if (Resource)
	{
		Resource->ReleaseRef();
	}

	Resource = Other.Resource;
	if (Resource)
	{
		Resource->AddRef();
	}
	return *this;
}

FResourceRef& FResourceRef::operator=(FResourceRef&& Other) noexcept
{
	if (this == &Other)
	{
		return *this;
	}

	if (Resource)
	{
		Resource->ReleaseRef();
	}

	Resource = Other.Resource;
	Other.Resource = nullptr;
	return *this;
}

void FResourceRef::Reset()
{
	if (Resource)
	{
		Resource->ReleaseRef();
		Resource = nullptr;
	}
}

} // namespace Catty
