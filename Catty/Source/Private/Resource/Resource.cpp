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

} // namespace Catty
