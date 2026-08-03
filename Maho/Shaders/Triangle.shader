#version 460

#pragma vertex vert_main
#pragma fragment frag_main

#include "MahoCommon.glsl"

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Color;

layout(location = 0) out vec4 out_Color;

void vert_main()
{
	gl_Position = MahoLocalToClip(a_Position);
	out_Color = vec4(a_Color, 1.0);
}

void frag_main()
{
	out_Color = out_Color;
}
