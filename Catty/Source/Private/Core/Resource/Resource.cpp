#include <Core/Resource/Resource.h>

#include <Core/Resource/ResourceManager.h>

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

void FResource::StaticTearDown(FResource* Resource)
{
	if (!Resource)
	{
		return;
	}

	// Drop catalog while Outer still valid (virtual path needs Package.ObjectName).
	UnregisterResource(Resource);
	if (Resource->GetId().IsValid())
	{
		ReleaseResourceId(Resource->GetId());
	}

	Resource->ClearOuter();
}

} // namespace Catty
