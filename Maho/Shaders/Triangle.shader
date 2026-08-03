#version 460

#pragma vertex main
#pragma fragment main

#include "MahoCommon.glsl"

#if defined(MAHO_SHADER_STAGE_VERTEX)

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Color;
layout(location = 0) out vec4 v_Color;

void main()
{
	gl_Position = MahoLocalToClip(a_Position);
	v_Color = vec4(a_Color, 1.0);
}

#elif defined(MAHO_SHADER_STAGE_FRAGMENT)

layout(location = 0) in vec4 v_Color;
layout(location = 0) out vec4 out_Color;

void main()
{
	out_Color = v_Color;
}

#endif