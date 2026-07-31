#include <Core/Extension/Resource/Resource.h>

namespace Maho
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

UMaterial::UMaterial(
	UPackage* InOuter,
	std::string InObjectName,
	EResourceType InType,
	std::string InSourcePath)
	: UResource(
		InOuter,
		std::move(InObjectName),
		InType == EResourceType::Unknown ? EResourceType::Material : InType,
		std::move(InSourcePath))
{
}

UMaterial::~UMaterial() = default;

UStaticMesh::UStaticMesh(
	UPackage* InOuter,
	std::string InObjectName,
	EResourceType InType,
	std::string InSourcePath)
	: UResource(
		InOuter,
		std::move(InObjectName),
		InType == EResourceType::Unknown ? EResourceType::Mesh : InType,
		std::move(InSourcePath))
{
}

UStaticMesh::~UStaticMesh() = default;

void UStaticMesh::SetCpuGeometry(
	std::vector<float> InPositions,
	std::vector<float> InNormals,
	std::vector<float> InUVs,
	std::vector<std::uint32_t> InIndices)
{
	Positions = std::move(InPositions);
	Normals = std::move(InNormals);
	UVs = std::move(InUVs);
	Indices = std::move(InIndices);
}

USkeleton::USkeleton(
	UPackage* InOuter,
	std::string InObjectName,
	EResourceType InType,
	std::string InSourcePath)
	: UResource(
		InOuter,
		std::move(InObjectName),
		InType == EResourceType::Unknown ? EResourceType::Skeleton : InType,
		std::move(InSourcePath))
{
}

USkeleton::~USkeleton() = default;

UAnimation::UAnimation(
	UPackage* InOuter,
	std::string InObjectName,
	EResourceType InType,
	std::string InSourcePath)
	: UResource(
		InOuter,
		std::move(InObjectName),
		InType == EResourceType::Unknown ? EResourceType::Animation : InType,
		std::move(InSourcePath))
{
}

UAnimation::~UAnimation() = default;

UAnimationGraph::UAnimationGraph(
	UPackage* InOuter,
	std::string InObjectName,
	EResourceType InType,
	std::string InSourcePath)
	: UResource(
		InOuter,
		std::move(InObjectName),
		InType == EResourceType::Unknown ? EResourceType::AnimationGraph : InType,
		std::move(InSourcePath))
{
}

UAnimationGraph::~UAnimationGraph() = default;

UPrefab::UPrefab(
	UPackage* InOuter,
	std::string InObjectName,
	EResourceType InType,
	std::string InSourcePath)
	: UResource(
		InOuter,
		std::move(InObjectName),
		InType == EResourceType::Unknown ? EResourceType::Prefab : InType,
		std::move(InSourcePath))
{
}

UPrefab::~UPrefab() = default;

} // namespace Maho
