#pragma once

// Optional Dear ImGui extensions (linked into Catty.dll via the imgui static lib).
#include <imgui.h>

#if defined(CATTY_WITH_IMGUI)
#	include <ImGuizmo.h>
#	include <implot.h>
#	include <imgui_node_editor.h>
#	include <ImGuiFileDialog.h>
#	include <IconsFontAwesome6.h>
#endif
