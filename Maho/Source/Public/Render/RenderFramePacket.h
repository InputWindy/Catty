#pragma once

/**
 * Immutable Game → MahoRender payload for one frame (value semantics).
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
	std::vector<FTextureCpuSnapshot> TextureUploads;
};

} // namespace Maho
