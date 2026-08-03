#pragma once

/**
 * Game → Render per-frame scene snapshot (value semantics).
 * Not Import/Export — small draw list + transforms only.
 * Full FScene (meshes / lights / cameras) comes later.
 */

#include <Core/Export.h>

#include <cstdint>
#include <vector>

namespace Maho
{

enum class EScenePrimitiveType : std::uint8_t
{
	ColoredTriangle = 0,
};

struct FSceneDrawItem
{
	EScenePrimitiveType Type = EScenePrimitiveType::ColoredTriangle;
	/** Row-major 4x4 LocalToWorld. */
	float LocalToWorld[16] = {
		1.f, 0.f, 0.f, 0.f,
		0.f, 1.f, 0.f, 0.f,
		0.f, 0.f, 1.f, 0.f,
		0.f, 0.f, 0.f, 1.f,
	};
};

struct MAHO_API FSceneUpdatePacket
{
	std::vector<FSceneDrawItem> Draws;
};

} // namespace Maho
