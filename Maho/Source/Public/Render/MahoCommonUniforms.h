#pragma once

#include <Core/Export.h>

namespace Maho
{

// Must match layout of FrameUniforms in MahoCommon.glsl
// set=0, binding=0, std140 layout
struct FFrameUniforms
{
	float View[16];              // offset 0,  size 64  (mat4)
	float Proj[16];              // offset 64, size 64  (mat4)
	float ViewProj[16];          // offset 128, size 64 (mat4)
	float CameraWorldPos[4];     // offset 192, size 16 (vec3 padded to 16 bytes in std140)
	float Time;                  // offset 208, size 4
	float Pad[3];                // offset 212, size 12 (padding to align next struct member)
};
static_assert(sizeof(FFrameUniforms) == 224, "FFrameUniforms size mismatch with GLSL std140 layout");

// Must match layout of ObjectUniforms in MahoCommon.glsl
// set=1, binding=0, std140 layout
struct FObjectUniforms
{
	float LocalToWorld[16];                 // offset 0,  size 64 (mat4)
	float LocalToWorldInverseTranspose[16]; // offset 64, size 64 (mat4)
};
static_assert(sizeof(FObjectUniforms) == 128, "FObjectUniforms size mismatch with GLSL std140 layout");

} // namespace Maho
