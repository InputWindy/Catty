#include <Core/Modules/Resource.h>
#include <Core/Extension/ResourceManager.h>

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
	if (FResourceManager* Manager = Detail::GetResourceManager())
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
