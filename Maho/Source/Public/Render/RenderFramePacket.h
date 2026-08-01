#pragma once

/**
 * Immutable Game → MahoRender payloads (value semantics).
 * Large resource blobs use CpuSnapshot + QueueResourceUpload; per-frame scene state is separate.
 */

#include <Core/Extension/Resource/Resource.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Maho
{

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

/** Interleaved P3 N3 UV2 vertices (+ optional skin streams later). */
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
	/** Row-major 4x4 inverse bind pose, BoneCount * 16. */
	std::vector<float> InverseBindPose;
};

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

struct FRenderFramePacket
{
	std::uint64_t FrameIndex = 0;
	float ClearColorR = 0.f;
	float ClearColorG = 0.f;
	float ClearColorB = 0.f;
	float ClearColorA = 1.f;
	int ImGuiSlotIndex = -1;
	bool bSubmitImGui = false;
	int FramebufferWidth = 0;
	int FramebufferHeight = 0;
	bool bResizeFramebuffer = false;
};

} // namespace Maho
