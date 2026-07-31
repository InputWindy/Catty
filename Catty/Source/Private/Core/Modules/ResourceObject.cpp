#include <Core/Extension/Resource.h>
#include <Core/Extension/Resource.h>

namespace Catty
{

UResource::UResource(
	UPackage* InOuter,
	std::string InObjectName,
	EResourceType InType,
	std::string InSourcePath)
	: UObject(InOuter, std::move(InObjectName))
	, Type(InType)
	, SourcePath(std::move(InSourcePath))
	, LoadState(EResourceLoadState::Pending)
{
}

UResource::~UResource() = default;

void UResource::OnPoolTearDown()
{
	if (FResourceSystem* Manager = Detail::GetResourceSystem())
	{
		Manager->UnregisterResource(this);
	}
}

UTextureResource::UTextureResource(
	UPackage* InOuter,
	std::string InObjectName,
	EResourceType InType,
	std::string InSourcePath)
	: UResource(
		InOuter,
		std::move(InObjectName),
		InType == EResourceType::Unknown ? EResourceType::Texture : InType,
		std::move(InSourcePath))
{
}

UTextureResource::~UTextureResource() = default;

} // namespace Catty
