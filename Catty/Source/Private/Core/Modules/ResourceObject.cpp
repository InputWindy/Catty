#include <Core/Modules/Resource.h>

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

	if (FResourceManager* Manager = Detail::GetResourceManager())
	{
		// Drop catalog while Outer still valid (virtual path needs Package.ObjectName).
		Manager->UnregisterResource(Resource);
		if (Resource->GetId().IsValid())
		{
			Manager->ReleaseResourceId(Resource->GetId());
		}
	}
}

} // namespace Catty
