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

UTexture::UTexture(
	UPackage* InOuter,
	std::string InObjectName,
	EResourceType InType,
	std::string InSourcePath)
	: UResource(
		InOuter,
		std::move(InObjectName),
		InType == EResourceType::Unknown ? EResourceType::Texture2D : InType,
		std::move(InSourcePath))
{
}

UTexture::~UTexture() = default;

void UTexture::SetCpuImage(
	ETextureDimension InDimension,
	ETexturePixelFormat InFormat,
	std::uint32_t InWidth,
	std::uint32_t InHeight,
	std::uint32_t InDepth,
	std::uint32_t InArrayLayers,
	std::uint32_t InMipCount,
	bool bInSRGB,
	std::vector<std::uint8_t> InPixels)
{
	Dimension = InDimension;
	PixelFormat = InFormat;
	Width = InWidth;
	Height = InHeight;
	Depth = InDepth == 0 ? 1 : InDepth;
	ArrayLayers = InArrayLayers == 0 ? 1 : InArrayLayers;
	MipCount = InMipCount == 0 ? 1 : InMipCount;
	bSRGB = bInSRGB;
	Pixels = std::move(InPixels);
}

UTexture2D::UTexture2D(
	UPackage* InOuter,
	std::string InObjectName,
	EResourceType InType,
	std::string InSourcePath)
	: UTexture(
		InOuter,
		std::move(InObjectName),
		InType == EResourceType::Unknown || InType == EResourceType::Texture
			? EResourceType::Texture2D
			: InType,
		std::move(InSourcePath))
{
	Dimension = ETextureDimension::Tex2D;
}

UTexture2D::~UTexture2D() = default;

UTexture3D::UTexture3D(
	UPackage* InOuter,
	std::string InObjectName,
	EResourceType InType,
	std::string InSourcePath)
	: UTexture(
		InOuter,
		std::move(InObjectName),
		InType == EResourceType::Unknown ? EResourceType::Texture3D : InType,
		std::move(InSourcePath))
{
	Dimension = ETextureDimension::Tex3D;
}

UTexture3D::~UTexture3D() = default;

UTextureCube::UTextureCube(
	UPackage* InOuter,
	std::string InObjectName,
	EResourceType InType,
	std::string InSourcePath)
	: UTexture(
		InOuter,
		std::move(InObjectName),
		InType == EResourceType::Unknown ? EResourceType::TextureCube : InType,
		std::move(InSourcePath))
{
	Dimension = ETextureDimension::Cube;
	ArrayLayers = 6;
}

UTextureCube::~UTextureCube() = default;

UTextureCubeArray::UTextureCubeArray(
	UPackage* InOuter,
	std::string InObjectName,
	EResourceType InType,
	std::string InSourcePath)
	: UTexture(
		InOuter,
		std::move(InObjectName),
		InType == EResourceType::Unknown ? EResourceType::TextureCubeArray : InType,
		std::move(InSourcePath))
{
	Dimension = ETextureDimension::CubeArray;
}

UTextureCubeArray::~UTextureCubeArray() = default;

UTexture2DArray::UTexture2DArray(
	UPackage* InOuter,
	std::string InObjectName,
	EResourceType InType,
	std::string InSourcePath)
	: UTexture(
		InOuter,
		std::move(InObjectName),
		InType == EResourceType::Unknown ? EResourceType::Texture2DArray : InType,
		std::move(InSourcePath))
{
	Dimension = ETextureDimension::Tex2DArray;
}

UTexture2DArray::~UTexture2DArray() = default;

} // namespace Catty
