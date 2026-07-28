#pragma once

// Dear ImGui build config for Catty (selected via IMGUI_USER_CONFIG).
#include <Core/Export.h>

#define IMGUI_API CATTY_API
#define USE_IMGUI_API
#define IMPLOT_API CATTY_API
#define IGFD_API CATTY_API
#define IMGUI_NODE_EDITOR_API CATTY_API

// Required by imgui-node-editor / several ImGui math helpers.
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#	define IMGUI_DEFINE_MATH_OPERATORS
#endif
