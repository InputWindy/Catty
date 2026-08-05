#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Maho
{

#ifdef MAHO_EXPORTS
#define MAHO_HAS_TEXTURE_PIXEL_FORMAT    1
#define MAHO_HAS_TEXTURE_DIMENSION       1
#define MAHO_HAS_ANIMATION_KEY           1
#endif

#ifndef MAHO_HAS_TEXTURE_PIXEL_FORMAT
#define MAHO_HAS_TEXTURE_PIXEL_FORMAT 1
#endif
#ifndef MAHO_HAS_TEXTURE_DIMENSION
#define MAHO_HAS_TEXTURE_DIMENSION    1
#endif
#ifndef MAHO_HAS_CPU_SNAPSHOTS
#define MAHO_HAS_CPU_SNAPSHOTS        1
#endif
#ifndef MAHO_HAS_ANIMATION_KEY
#define MAHO_HAS_ANIMATION_KEY        1
#endif

#if MAHO_HAS_TEXTURE_PIXEL_FORMAT
// ── Resource format enums (minimal, used by proxy registries) ──
enum class ETexturePixelFormat : std::uint8_t
{
	Unknown = 0,
	RGBA8,
	RGBA16F,
	RGBA32F,
	R8,
	RG8,
	RGB8,
	BlockCompressed,
	R16F,
	DXT1,
	DXT5,
	BC7,
	Count,
};
#endif // MAHO_HAS_TEXTURE_PIXEL_FORMAT

#if MAHO_HAS_TEXTURE_DIMENSION
enum class ETextureDimension : std::uint8_t
{
	Unknown = 0,
	Tex1D,
	Tex2D,
	Tex3D,
	TexCube,
	Cube = TexCube,
	TexCubeArray,
	Tex2DArray,
	CubeArray = TexCubeArray, // Project alias
	Count,
};
#endif // MAHO_HAS_TEXTURE_DIMENSION

#if MAHO_HAS_CPU_SNAPSHOTS
// ── CPU snapshots (Game-thread submit to render thread) ──

struct FTextureCpuSnapshot
{
	std::string CatalogKey;
	std::uint64_t Generation = 0;
	ETextureDimension Dimension = ETextureDimension::Tex2D;
	ETexturePixelFormat PixelFormat = ETexturePixelFormat::Unknown;
	std::uint32_t Width = 0;
	std::uint32_t Height = 0;
	std::uint32_t Depth = 1;
	std::uint32_t ArrayLayers = 1;
	std::uint32_t MipCount = 1;
	bool bSRGB = true;
	std::vector<std::uint8_t> Pixels;
};

struct FMeshCpuSnapshot
{
	std::string CatalogKey;
	std::uint64_t Generation = 0;
	std::vector<std::uint8_t> InterleavedVertices;
	std::vector<std::uint32_t> Indices;
	std::uint32_t VertexStride = 0;
	std::uint32_t VertexCount = 0;
	bool bHasSkinning = false;
};

struct FSkeletonCpuSnapshot
{
	std::string CatalogKey;
	std::uint64_t Generation = 0;
	std::uint32_t BoneCount = 0;
	std::vector<std::string> BoneNames;
	std::vector<std::int32_t> ParentIndex;
	std::vector<float> InverseBindPose;
};

#if MAHO_HAS_ANIMATION_KEY
#if MAHO_HAS_ANIMATION_KEY
struct FAnimationKey
{
	float Time = 0.f;
	float Translation[3] = {0, 0, 0};
	float Rotation[4] = {0, 0, 0, 1}; // xyzw
	float Scale[3] = {1, 1, 1};
};
#endif
#endif // MAHO_HAS_ANIMATION_KEY

struct FAnimationTrackSnapshot
{
	std::string TargetBoneName;
	std::int32_t TargetBoneIndex = -1;
	std::vector<FAnimationKey> Keys;
};

struct FAnimationCpuSnapshot
{
	std::string CatalogKey;
	std::uint64_t Generation = 0;
	std::string SkeletonCatalogKey;
	float DurationSeconds = 0.f;
	std::vector<FAnimationTrackSnapshot> Tracks;
};
#endif // MAHO_HAS_CPU_SNAPSHOTS

} // namespace Maho
