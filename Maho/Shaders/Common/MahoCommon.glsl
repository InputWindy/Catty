// MahoCommon.glsl — Engine-injected shared header for all shaders
// Provides automatic frame/object uniform blocks and convenience functions.
//
// Usage in .shader files:
//   #include "MahoCommon.glsl"

#ifndef MAHO_COMMON_GLSL
#define MAHO_COMMON_GLSL

// === Engine frame uniform block (set=0, binding=0) ===
// Updated once per frame by the engine.
layout(set = 0, binding = 0) uniform FrameUniforms
{
	mat4 View;
	mat4 Proj;
	mat4 ViewProj;
	vec3 CameraWorldPos;
	float Time;
} u_Frame;

// === Engine per-object uniform block (set=1, binding=0) ===
// Updated per draw call by the engine.
layout(set = 1, binding = 0) uniform ObjectUniforms
{
	mat4 LocalToWorld;
	mat4 LocalToWorldInverseTranspose;  // For normal transformation
} u_Object;

// === Convenience functions ===

// Transform from object-space to clip space.
vec4 MahoLocalToClip(vec3 localPos)
{
	return u_Frame.ViewProj * u_Object.LocalToWorld * vec4(localPos, 1.0);
}

// Transform normal from object-space to world-space (handles non-uniform scale correctly).
vec3 MahoLocalToWorldNormal(vec3 localNormal)
{
	return normalize(mat3(u_Object.LocalToWorldInverseTranspose) * localNormal);
}

// Get world-space position of the object origin.
vec3 MahoWorldPos()
{
	return u_Object.LocalToWorld[3].xyz;
}

#endif // MAHO_COMMON_GLSL
