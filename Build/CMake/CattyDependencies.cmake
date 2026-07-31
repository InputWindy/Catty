# Third-party dependencies for the Catty engine target.

include(FetchContent)

# VS rebuilds re-run CMake when this file changes. Skip git "update" so offline /
# flaky GitHub access does not break an already-populated Intermediate/_deps tree.
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL
	"Skip FetchContent git update when already populated" FORCE)

# If a previous populate left sources but lost CMake stamps, reuse the tree
# instead of calling FetchContent_Populate (which may still hit the network).
# Markers: relative paths that must exist under ${FETCHCONTENT_BASE_DIR}/<name>-src.
macro(catty_fetchcontent_populate_or_reuse _name)
	FetchContent_GetProperties(${_name})
	if(NOT ${_name}_POPULATED)
		string(TOLOWER "${_name}" _catty_fc_lower)
		set(_catty_fc_src "${FETCHCONTENT_BASE_DIR}/${_catty_fc_lower}-src")
		set(_catty_fc_ok FALSE)
		foreach(_catty_fc_marker IN ITEMS ${ARGN})
			if(EXISTS "${_catty_fc_src}/${_catty_fc_marker}")
				set(_catty_fc_ok TRUE)
				break()
			endif()
		endforeach()
		if(_catty_fc_ok)
			set(${_name}_SOURCE_DIR "${_catty_fc_src}")
			set(${_name}_BINARY_DIR "${FETCHCONTENT_BASE_DIR}/${_catty_fc_lower}-build")
			set(${_name}_POPULATED TRUE)
			message(STATUS "Catty: reusing FetchContent ${_name} at ${${_name}_SOURCE_DIR}")
		else()
			FetchContent_Populate(${_name})
		endif()
		unset(_catty_fc_src)
		unset(_catty_fc_ok)
		unset(_catty_fc_lower)
		unset(_catty_fc_marker)
	endif()
endmacro()

set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
	spdlog
	GIT_REPOSITORY https://github.com/gabime/spdlog.git
	GIT_TAG v1.15.3
	GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(spdlog)

if(TARGET spdlog)
	set_target_properties(spdlog PROPERTIES FOLDER "ThirdParty")
endif()

# GLFW (window / input / time; CLIENT_API=NO_API for future Vulkan surfaces)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

set(_CATTY_BUILD_SHARED_LIBS_SAVED "${BUILD_SHARED_LIBS}")
set(BUILD_SHARED_LIBS OFF)

FetchContent_Declare(
	glfw
	GIT_REPOSITORY https://github.com/glfw/glfw.git
	GIT_TAG 3.4
	GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(glfw)

set(BUILD_SHARED_LIBS "${_CATTY_BUILD_SHARED_LIBS_SAVED}")
unset(_CATTY_BUILD_SHARED_LIBS_SAVED)

# GLFW's own CMake uses FOLDER "GLFW3"; keep all GLFW targets under ThirdParty in the .sln.
if(TARGET glfw)
	set_target_properties(glfw PROPERTIES FOLDER "ThirdParty")
endif()
if(TARGET update_mappings)
	set_target_properties(update_mappings PROPERTIES FOLDER "ThirdParty")
endif()

# Vulkan (LunarG SDK via VULKAN_SDK / FindVulkan)
find_package(Vulkan REQUIRED)
message(STATUS "Catty: Vulkan found")
message(STATUS "  Vulkan_INCLUDE_DIRS = ${Vulkan_INCLUDE_DIRS}")
message(STATUS "  Vulkan_LIBRARIES    = ${Vulkan_LIBRARIES}")

find_package(Threads REQUIRED)

get_filename_component(_CATTY_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
get_filename_component(_CATTY_PUBLIC_HEADERS "${_CATTY_REPO_ROOT}/Catty/Source/Public" ABSOLUTE)

# Include as <nlohmann/json.hpp> (vendored single-header under Catty/ThirdParty/nlohmann).
set(_CATTY_VENDORED_NLOHMANN_JSON "${_CATTY_REPO_ROOT}/Catty/ThirdParty/nlohmann")
if(EXISTS "${_CATTY_VENDORED_NLOHMANN_JSON}/json.hpp")
	set(CATTY_NLOHMANN_JSON_INCLUDE_DIR "${_CATTY_REPO_ROOT}/Catty/ThirdParty" CACHE INTERNAL "nlohmann/json include root")
	message(STATUS "Catty: nlohmann/json (vendored) at ${_CATTY_VENDORED_NLOHMANN_JSON}")
else()
	FetchContent_Declare(
		nlohmann_json
		GIT_REPOSITORY https://github.com/nlohmann/json.git
		GIT_TAG v3.11.3
		GIT_SHALLOW TRUE
	)
	FetchContent_MakeAvailable(nlohmann_json)
	# single_include/nlohmann/json.hpp layout from the repo.
	set(CATTY_NLOHMANN_JSON_INCLUDE_DIR "${nlohmann_json_SOURCE_DIR}/single_include" CACHE INTERNAL "nlohmann/json include root")
	message(STATUS "Catty: nlohmann/json (FetchContent) at ${CATTY_NLOHMANN_JSON_INCLUDE_DIR}")
endif()
unset(_CATTY_VENDORED_NLOHMANN_JSON)

# ---------------------------------------------------------------------------
# Dear ImGui + extensions via FetchContent (do not vendor these trees in git).
# ---------------------------------------------------------------------------
FetchContent_Declare(
	imgui
	GIT_REPOSITORY https://github.com/ocornut/imgui.git
	GIT_TAG v1.91.9-docking
	GIT_SHALLOW TRUE
)
catty_fetchcontent_populate_or_reuse(imgui imgui.cpp)
set(CATTY_IMGUI_SOURCE_DIR "${imgui_SOURCE_DIR}" CACHE INTERNAL "Dear ImGui source directory" FORCE)
message(STATUS "Catty: ImGui (FetchContent) at ${CATTY_IMGUI_SOURCE_DIR}")

# Flush-left dock tabs without clearing FramePadding (needed for centered tab labels).
# Also skip WindowBorderSize inset so enabling window borders does not shift tabs 1px.
# Idempotent: skips if already patched.
set(_CATTY_IMGUI_CPP "${CATTY_IMGUI_SOURCE_DIR}/imgui.cpp")
if(EXISTS "${_CATTY_IMGUI_CPP}")
	file(READ "${_CATTY_IMGUI_CPP}" _CATTY_IMGUI_CONTENTS)
	if(NOT _CATTY_IMGUI_CONTENTS MATCHES "Catty: dock tab bar flush-left")
		string(REPLACE
			"float button_sz = g.FontSize;\n    r.Min.x += style.FramePadding.x;\n    r.Max.x -= style.FramePadding.x;\n    ImVec2 window_menu_button_pos"
			"float button_sz = g.FontSize;\n    // Catty: dock tab bar flush-left — keep FramePadding for TabItem label centering.\n    // Upstream insets the whole bar by FramePadding.x, which prevents flush-left tabs.\n    // r.Min.x += style.FramePadding.x;\n    // r.Max.x -= style.FramePadding.x;\n    ImVec2 window_menu_button_pos"
			_CATTY_IMGUI_CONTENTS "${_CATTY_IMGUI_CONTENTS}")
		if(_CATTY_IMGUI_CONTENTS MATCHES "Catty: dock tab bar flush-left")
			file(WRITE "${_CATTY_IMGUI_CPP}" "${_CATTY_IMGUI_CONTENTS}")
			message(STATUS "Catty: patched ImGui DockNodeCalcTabBarLayout for flush-left tabs")
		else()
			message(WARNING "Catty: failed to patch ImGui dock tab-bar FramePadding inset (source layout changed?)")
		endif()
	endif()
	file(READ "${_CATTY_IMGUI_CPP}" _CATTY_IMGUI_CONTENTS)
	if(NOT _CATTY_IMGUI_CONTENTS MATCHES "Catty: do not inset tab bar by WindowBorderSize")
		string(REPLACE
			"if (out_title_rect) { *out_title_rect = r; }\n\n    r.Min.x += style.WindowBorderSize;\n    r.Max.x -= style.WindowBorderSize;\n\n    float button_sz = g.FontSize;"
			"if (out_title_rect) { *out_title_rect = r; }\n\n    // Catty: do not inset tab bar by WindowBorderSize (keeps tabs flush with panel edge when borders are on).\n    // r.Min.x += style.WindowBorderSize;\n    // r.Max.x -= style.WindowBorderSize;\n\n    float button_sz = g.FontSize;"
			_CATTY_IMGUI_CONTENTS "${_CATTY_IMGUI_CONTENTS}")
		if(_CATTY_IMGUI_CONTENTS MATCHES "Catty: do not inset tab bar by WindowBorderSize")
			file(WRITE "${_CATTY_IMGUI_CPP}" "${_CATTY_IMGUI_CONTENTS}")
			message(STATUS "Catty: patched ImGui DockNodeCalcTabBarLayout WindowBorderSize inset")
		else()
			message(WARNING "Catty: failed to patch ImGui dock tab-bar WindowBorderSize inset (source layout changed?)")
		endif()
	endif()
endif()
unset(_CATTY_IMGUI_CPP)
unset(_CATTY_IMGUI_CONTENTS)

# ImGuizmo master (v1.9+) lays sources under src/; pin master + use that subdir.
FetchContent_Declare(
	imguizmo
	GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
	GIT_TAG master
	GIT_SHALLOW TRUE
)
catty_fetchcontent_populate_or_reuse(imguizmo src/ImGuizmo.cpp ImGuizmo.cpp)
if(EXISTS "${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp")
	set(CATTY_IMGUIZMO_SOURCE_DIR "${imguizmo_SOURCE_DIR}/src" CACHE INTERNAL "ImGuizmo source directory" FORCE)
elseif(EXISTS "${imguizmo_SOURCE_DIR}/ImGuizmo.cpp")
	set(CATTY_IMGUIZMO_SOURCE_DIR "${imguizmo_SOURCE_DIR}" CACHE INTERNAL "ImGuizmo source directory" FORCE)
else()
	message(FATAL_ERROR "Catty: ImGuizmo sources not found under ${imguizmo_SOURCE_DIR}")
endif()

FetchContent_Declare(
	imgui_node_editor
	GIT_REPOSITORY https://github.com/thedmd/imgui-node-editor.git
	# v0.9.3 predates ImGui 1.90+ (ImVec2 ops / GetKeyIndex removal); develop tracks current ImGui.
	GIT_TAG develop
	GIT_SHALLOW TRUE
)
catty_fetchcontent_populate_or_reuse(imgui_node_editor imgui_node_editor.cpp)
set(CATTY_IMGUI_NODE_EDITOR_SOURCE_DIR "${imgui_node_editor_SOURCE_DIR}" CACHE INTERNAL "imgui-node-editor source directory" FORCE)

FetchContent_Declare(
	implot
	GIT_REPOSITORY https://github.com/epezent/implot.git
	GIT_TAG v0.16
	GIT_SHALLOW TRUE
)
catty_fetchcontent_populate_or_reuse(implot implot.cpp)
set(CATTY_IMPLOT_SOURCE_DIR "${implot_SOURCE_DIR}" CACHE INTERNAL "ImPlot source directory" FORCE)

FetchContent_Declare(
	imgui_file_dialog
	GIT_REPOSITORY https://github.com/aiekick/ImGuiFileDialog.git
	GIT_TAG v0.6.7
	GIT_SHALLOW TRUE
)
catty_fetchcontent_populate_or_reuse(imgui_file_dialog ImGuiFileDialog.cpp)
set(CATTY_IMGUI_FILE_DIALOG_SOURCE_DIR "${imgui_file_dialog_SOURCE_DIR}" CACHE INTERNAL "ImGuiFileDialog source directory" FORCE)

FetchContent_Declare(
	icon_font_cpp_headers
	GIT_REPOSITORY https://github.com/juliettef/IconFontCppHeaders.git
	GIT_TAG main
	GIT_SHALLOW TRUE
)
catty_fetchcontent_populate_or_reuse(icon_font_cpp_headers IconsFontAwesome6.h)
set(CATTY_ICON_FONT_HEADERS_DIR "${icon_font_cpp_headers_SOURCE_DIR}" CACHE INTERNAL "IconFontCppHeaders include directory" FORCE)

message(STATUS "Catty: ImGuizmo            ${CATTY_IMGUIZMO_SOURCE_DIR}")
message(STATUS "Catty: imgui-node-editor   ${CATTY_IMGUI_NODE_EDITOR_SOURCE_DIR}")
message(STATUS "Catty: ImPlot              ${CATTY_IMPLOT_SOURCE_DIR}")
message(STATUS "Catty: ImGuiFileDialog     ${CATTY_IMGUI_FILE_DIALOG_SOURCE_DIR}")
message(STATUS "Catty: IconFontCppHeaders  ${CATTY_ICON_FONT_HEADERS_DIR}")

set(CATTY_IMGUI_SOURCES
	"${CATTY_IMGUI_SOURCE_DIR}/imgui.cpp"
	"${CATTY_IMGUI_SOURCE_DIR}/imgui_demo.cpp"
	"${CATTY_IMGUI_SOURCE_DIR}/imgui_draw.cpp"
	"${CATTY_IMGUI_SOURCE_DIR}/imgui_tables.cpp"
	"${CATTY_IMGUI_SOURCE_DIR}/imgui_widgets.cpp"
	"${CATTY_IMGUI_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
	"${CATTY_IMGUI_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
)

set(CATTY_IMGUI_EXT_SOURCES
	"${CATTY_IMGUIZMO_SOURCE_DIR}/ImGuizmo.cpp"
	"${CATTY_IMGUIZMO_SOURCE_DIR}/ImCurveEdit.cpp"
	"${CATTY_IMGUIZMO_SOURCE_DIR}/ImGradient.cpp"
	"${CATTY_IMGUI_NODE_EDITOR_SOURCE_DIR}/imgui_node_editor.cpp"
	"${CATTY_IMGUI_NODE_EDITOR_SOURCE_DIR}/imgui_node_editor_api.cpp"
	"${CATTY_IMGUI_NODE_EDITOR_SOURCE_DIR}/imgui_canvas.cpp"
	"${CATTY_IMGUI_NODE_EDITOR_SOURCE_DIR}/crude_json.cpp"
	"${CATTY_IMPLOT_SOURCE_DIR}/implot.cpp"
	"${CATTY_IMPLOT_SOURCE_DIR}/implot_items.cpp"
	"${CATTY_IMPLOT_SOURCE_DIR}/implot_demo.cpp"
	"${CATTY_IMGUI_FILE_DIALOG_SOURCE_DIR}/ImGuiFileDialog.cpp"
)

# STATIC ThirdParty lib (own .vcxproj under ThirdParty/). Avoid OBJECT — VS would
# nest imgui*.obj under Catty's "Object Libraries" filter.
add_library(imgui STATIC ${CATTY_IMGUI_SOURCES} ${CATTY_IMGUI_EXT_SOURCES})
add_library(Catty::ImGui ALIAS imgui)

target_include_directories(imgui
	PUBLIC
		"${CATTY_IMGUI_SOURCE_DIR}"
		"${CATTY_IMGUI_SOURCE_DIR}/backends"
		# IMGUI_USER_CONFIG resolves to Render/UI/ImGuiConfig.h under this include root.
		"${_CATTY_PUBLIC_HEADERS}"
		"${CATTY_IMGUIZMO_SOURCE_DIR}"
		"${CATTY_IMGUI_NODE_EDITOR_SOURCE_DIR}"
		"${CATTY_IMPLOT_SOURCE_DIR}"
		"${CATTY_IMGUI_FILE_DIALOG_SOURCE_DIR}"
		"${CATTY_ICON_FONT_HEADERS_DIR}"
)

target_compile_definitions(imgui
	PUBLIC
		IMGUI_USER_CONFIG="Render/UI/ImGuiConfig.h"
		CATTY_WITH_IMGUI=1
		CATTY_WITH_IMGUI_EXTENSIONS=1
		USE_IMGUI_API=1
		USE_STD_FILESYSTEM=1
	PRIVATE
		$<$<BOOL:${CATTY_BUILD_SHARED}>:CATTY_BUILD_SHARED=1>
		$<$<BOOL:${CATTY_BUILD_SHARED}>:CATTY_EXPORTS=1>
)

target_link_libraries(imgui
	PUBLIC
		Vulkan::Vulkan
	PRIVATE
		glfw
)

target_compile_features(imgui PUBLIC cxx_std_20)

if(MSVC)
	target_compile_options(imgui PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8)
	set_source_files_properties(${CATTY_IMGUI_EXT_SOURCES} PROPERTIES COMPILE_FLAGS "/W3")
endif()

set_target_properties(imgui PROPERTIES
	FOLDER "ThirdParty"
	POSITION_INDEPENDENT_CODE ON
)

source_group(TREE "${CATTY_IMGUI_SOURCE_DIR}" PREFIX "imgui" FILES ${CATTY_IMGUI_SOURCES})
source_group("imgui_ext" FILES ${CATTY_IMGUI_EXT_SOURCES})
list(LENGTH CATTY_IMGUI_EXT_SOURCES _CATTY_IMGUI_EXT_COUNT)
message(STATUS "Catty: ImGui extensions: ${_CATTY_IMGUI_EXT_COUNT} source file(s)")
unset(_CATTY_IMGUI_EXT_COUNT)

# Engine fonts stay in-repo (binary assets copied next to the binary at build).
set(_CATTY_TP "${_CATTY_REPO_ROOT}/Catty/ThirdParty")
set(CATTY_ENGINE_FONTS_DIR "${_CATTY_TP}/fonts" CACHE INTERNAL "Engine icon/UI fonts")

# ---------------------------------------------------------------------------
# Lua 5.4 (static) + sol2 (header-only bindings)
# ---------------------------------------------------------------------------
set(_CATTY_VENDORED_LUA "${_CATTY_REPO_ROOT}/Catty/ThirdParty/lua")
if(EXISTS "${_CATTY_VENDORED_LUA}/lua.h")
	set(CATTY_LUA_SOURCE_DIR "${_CATTY_VENDORED_LUA}" CACHE INTERNAL "Lua source directory")
	message(STATUS "Catty: Lua (vendored) at ${CATTY_LUA_SOURCE_DIR}")
elseif(EXISTS "${_CATTY_VENDORED_LUA}/src/lua.h")
	set(CATTY_LUA_SOURCE_DIR "${_CATTY_VENDORED_LUA}/src" CACHE INTERNAL "Lua source directory")
	message(STATUS "Catty: Lua (vendored src/) at ${CATTY_LUA_SOURCE_DIR}")
else()
	FetchContent_Declare(
		lua_src
		GIT_REPOSITORY https://github.com/lua/lua.git
		GIT_TAG v5.4.7
		GIT_SHALLOW TRUE
	)
	catty_fetchcontent_populate_or_reuse(lua_src lua.h)
	# Upstream lua.git keeps sources in the repo root (not src/).
	set(CATTY_LUA_SOURCE_DIR "${lua_src_SOURCE_DIR}" CACHE INTERNAL "Lua source directory")
	message(STATUS "Catty: Lua (FetchContent) at ${CATTY_LUA_SOURCE_DIR}")
endif()

set(CATTY_LUA_SOURCES
	"${CATTY_LUA_SOURCE_DIR}/lapi.c"
	"${CATTY_LUA_SOURCE_DIR}/lauxlib.c"
	"${CATTY_LUA_SOURCE_DIR}/lbaselib.c"
	"${CATTY_LUA_SOURCE_DIR}/lcode.c"
	"${CATTY_LUA_SOURCE_DIR}/lcorolib.c"
	"${CATTY_LUA_SOURCE_DIR}/lctype.c"
	"${CATTY_LUA_SOURCE_DIR}/ldblib.c"
	"${CATTY_LUA_SOURCE_DIR}/ldebug.c"
	"${CATTY_LUA_SOURCE_DIR}/ldo.c"
	"${CATTY_LUA_SOURCE_DIR}/ldump.c"
	"${CATTY_LUA_SOURCE_DIR}/lfunc.c"
	"${CATTY_LUA_SOURCE_DIR}/lgc.c"
	"${CATTY_LUA_SOURCE_DIR}/linit.c"
	"${CATTY_LUA_SOURCE_DIR}/liolib.c"
	"${CATTY_LUA_SOURCE_DIR}/llex.c"
	"${CATTY_LUA_SOURCE_DIR}/lmathlib.c"
	"${CATTY_LUA_SOURCE_DIR}/lmem.c"
	"${CATTY_LUA_SOURCE_DIR}/loadlib.c"
	"${CATTY_LUA_SOURCE_DIR}/lobject.c"
	"${CATTY_LUA_SOURCE_DIR}/lopcodes.c"
	"${CATTY_LUA_SOURCE_DIR}/loslib.c"
	"${CATTY_LUA_SOURCE_DIR}/lparser.c"
	"${CATTY_LUA_SOURCE_DIR}/lstate.c"
	"${CATTY_LUA_SOURCE_DIR}/lstring.c"
	"${CATTY_LUA_SOURCE_DIR}/lstrlib.c"
	"${CATTY_LUA_SOURCE_DIR}/ltable.c"
	"${CATTY_LUA_SOURCE_DIR}/ltablib.c"
	"${CATTY_LUA_SOURCE_DIR}/ltm.c"
	"${CATTY_LUA_SOURCE_DIR}/lundump.c"
	"${CATTY_LUA_SOURCE_DIR}/lutf8lib.c"
	"${CATTY_LUA_SOURCE_DIR}/lvm.c"
	"${CATTY_LUA_SOURCE_DIR}/lzio.c"
)

add_library(lua STATIC ${CATTY_LUA_SOURCES})
add_library(Catty::Lua ALIAS lua)
target_include_directories(lua PUBLIC "${CATTY_LUA_SOURCE_DIR}")
set_target_properties(lua PROPERTIES
	FOLDER "ThirdParty"
	POSITION_INDEPENDENT_CODE ON
	C_STANDARD 99
)
if(MSVC)
	target_compile_definitions(lua PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

set(_CATTY_VENDORED_SOL2 "${_CATTY_REPO_ROOT}/Catty/ThirdParty/sol2")
if(EXISTS "${_CATTY_VENDORED_SOL2}/include/sol/sol.hpp")
	set(CATTY_SOL2_INCLUDE_DIR "${_CATTY_VENDORED_SOL2}/include" CACHE INTERNAL "sol2 include directory")
	message(STATUS "Catty: sol2 (vendored) at ${CATTY_SOL2_INCLUDE_DIR}")
else()
	FetchContent_Declare(
		sol2
		GIT_REPOSITORY https://github.com/ThePhD/sol2.git
		GIT_TAG v3.3.1
		GIT_SHALLOW TRUE
	)
	catty_fetchcontent_populate_or_reuse(sol2 include/sol/sol.hpp)
	set(CATTY_SOL2_INCLUDE_DIR "${sol2_SOURCE_DIR}/include" CACHE INTERNAL "sol2 include directory")
	message(STATUS "Catty: sol2 (FetchContent) at ${CATTY_SOL2_INCLUDE_DIR}")
endif()

unset(_CATTY_VENDORED_LUA)
unset(_CATTY_VENDORED_SOL2)

# ---------------------------------------------------------------------------
# refl-cpp (header-only) — optional / legacy; FObject reflection uses ObjectReflect.h + codegen.
# ---------------------------------------------------------------------------
set(_CATTY_VENDORED_REFL "${_CATTY_REPO_ROOT}/Catty/ThirdParty/refl-cpp")
if(EXISTS "${_CATTY_VENDORED_REFL}/include/refl.hpp")
	set(CATTY_REFL_INCLUDE_DIR "${_CATTY_VENDORED_REFL}/include" CACHE INTERNAL "refl-cpp include directory")
	message(STATUS "Catty: refl-cpp (vendored) at ${CATTY_REFL_INCLUDE_DIR}")
elseif(EXISTS "${_CATTY_VENDORED_REFL}/refl.hpp")
	set(CATTY_REFL_INCLUDE_DIR "${_CATTY_VENDORED_REFL}" CACHE INTERNAL "refl-cpp include directory")
	message(STATUS "Catty: refl-cpp (vendored flat) at ${CATTY_REFL_INCLUDE_DIR}")
else()
	FetchContent_Declare(
		refl_cpp
		GIT_REPOSITORY https://github.com/veselink1/refl-cpp.git
		GIT_TAG v0.12.4
		GIT_SHALLOW TRUE
	)
	catty_fetchcontent_populate_or_reuse(refl_cpp include/refl.hpp)
	set(CATTY_REFL_INCLUDE_DIR "${refl_cpp_SOURCE_DIR}/include" CACHE INTERNAL "refl-cpp include directory")
	message(STATUS "Catty: refl-cpp (FetchContent) at ${CATTY_REFL_INCLUDE_DIR}")
endif()
unset(_CATTY_VENDORED_REFL)

# -----------------------------------------------------------------------------
# Vulkan Memory Allocator (GPUOpen) — header-only; VMA_IMPLEMENTATION in one Catty .cpp
# Prefer vendored tree; FetchContent only if missing (needs network).
# -----------------------------------------------------------------------------
set(_CATTY_VENDORED_VMA "${_CATTY_REPO_ROOT}/Catty/ThirdParty/VulkanMemoryAllocator")
if(EXISTS "${_CATTY_VENDORED_VMA}/include/vk_mem_alloc.h")
	set(CATTY_VMA_INCLUDE_DIR "${_CATTY_VENDORED_VMA}/include" CACHE INTERNAL "VulkanMemoryAllocator include directory" FORCE)
	message(STATUS "Catty: VMA (vendored) at ${CATTY_VMA_INCLUDE_DIR}")
else()
	FetchContent_Declare(
		vma
		GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
		GIT_TAG v3.2.1
		GIT_SHALLOW TRUE
	)
	catty_fetchcontent_populate_or_reuse(vma include/vk_mem_alloc.h)
	set(CATTY_VMA_INCLUDE_DIR "${vma_SOURCE_DIR}/include" CACHE INTERNAL "VulkanMemoryAllocator include directory" FORCE)
	message(STATUS "Catty: VMA (FetchContent) at ${CATTY_VMA_INCLUDE_DIR}")
endif()
unset(_CATTY_VENDORED_VMA)

# -----------------------------------------------------------------------------
# KTX-Software (libktx) — KTX2 Import/Export for UTexture* (Game CPU path)
# OpenImageIO remains the preferred raster codec upgrade; Win32 uses WIC until then.
#
# Enable when sources are available:
#   -DCATTY_KTX_SOURCE_DIR=<checkout>   OR pre-populate Intermediate/_deps/ktx_software-src
#   -DCATTY_FETCH_LIBKTX=ON             to FetchContent from GitHub (needs network)
# Without sources, configure succeeds; KTX2 path is compile-disabled (WIC still works).
# -----------------------------------------------------------------------------
option(CATTY_WITH_LIBKTX "Build with libktx when KTX-Software sources are available" ON)
option(CATTY_FETCH_LIBKTX "FetchContent KTX-Software from GitHub (slow / needs network)" OFF)
set(CATTY_HAS_LIBKTX 0)
set(CATTY_KTX_SOURCE_DIR "" CACHE PATH "Pre-cloned KTX-Software root (optional)")
if(CATTY_WITH_LIBKTX)
	set(KTX_FEATURE_TOOLS OFF CACHE BOOL "" FORCE)
	set(KTX_FEATURE_DOC OFF CACHE BOOL "" FORCE)
	set(KTX_FEATURE_TESTS OFF CACHE BOOL "" FORCE)
	set(KTX_FEATURE_GL_UPLOAD OFF CACHE BOOL "" FORCE)
	set(KTX_FEATURE_VK_UPLOAD OFF CACHE BOOL "" FORCE)
	set(KTX_FEATURE_STATIC_LIBRARY ON CACHE BOOL "" FORCE)

	set(_CATTY_KTX_SRC "")
	if(CATTY_KTX_SOURCE_DIR AND EXISTS "${CATTY_KTX_SOURCE_DIR}/CMakeLists.txt")
		set(_CATTY_KTX_SRC "${CATTY_KTX_SOURCE_DIR}")
		message(STATUS "Catty: using CATTY_KTX_SOURCE_DIR=${_CATTY_KTX_SRC}")
	else()
		set(_catty_ktx_reuse "${FETCHCONTENT_BASE_DIR}/ktx_software-src")
		if(EXISTS "${_catty_ktx_reuse}/CMakeLists.txt")
			set(_CATTY_KTX_SRC "${_catty_ktx_reuse}")
			message(STATUS "Catty: reusing KTX-Software at ${_CATTY_KTX_SRC}")
		elseif(CATTY_FETCH_LIBKTX)
			FetchContent_Declare(
				ktx_software
				GIT_REPOSITORY https://github.com/KhronosGroup/KTX-Software.git
				GIT_TAG v4.3.2
				GIT_SHALLOW TRUE
			)
			FetchContent_GetProperties(ktx_software)
			if(NOT ktx_software_POPULATED)
				FetchContent_Populate(ktx_software)
			endif()
			if(DEFINED ktx_software_SOURCE_DIR AND EXISTS "${ktx_software_SOURCE_DIR}/CMakeLists.txt")
				set(_CATTY_KTX_SRC "${ktx_software_SOURCE_DIR}")
			endif()
		endif()
		unset(_catty_ktx_reuse)
	endif()

	if(_CATTY_KTX_SRC AND EXISTS "${_CATTY_KTX_SRC}/CMakeLists.txt")
		# KTX-Software FindBash requires a Unix-ish bash (Git for Windows).
		if(WIN32 AND NOT BASH_EXECUTABLE)
			foreach(_catty_bash_candidate
				"$ENV{ProgramFiles}/Git/bin/bash.exe"
				"$ENV{ProgramFiles\(x86\)}/Git/bin/bash.exe"
				"D:/Git/bin/bash.exe"
				"C:/Program Files/Git/bin/bash.exe")
				if(EXISTS "${_catty_bash_candidate}")
					set(BASH_EXECUTABLE "${_catty_bash_candidate}" CACHE FILEPATH "bash for KTX-Software" FORCE)
					break()
				endif()
			endforeach()
			# Same install as `where git` → …/cmd/git.exe → sibling bin/bash.exe
			if(NOT BASH_EXECUTABLE)
				find_program(_catty_git_exe NAMES git git.exe)
				if(_catty_git_exe)
					get_filename_component(_catty_git_cmd "${_catty_git_exe}" DIRECTORY)
					get_filename_component(_catty_git_root "${_catty_git_cmd}" DIRECTORY)
					if(EXISTS "${_catty_git_root}/bin/bash.exe")
						set(BASH_EXECUTABLE "${_catty_git_root}/bin/bash.exe" CACHE FILEPATH "bash for KTX-Software" FORCE)
					endif()
					unset(_catty_git_cmd)
					unset(_catty_git_root)
				endif()
				unset(_catty_git_exe)
			endif()
		endif()
		set(ktx_software_SOURCE_DIR "${_CATTY_KTX_SRC}")
		set(ktx_software_BINARY_DIR "${FETCHCONTENT_BASE_DIR}/ktx_software-build")
		# Parent game projects often declare LANGUAGES CXX only; libktx needs C.
		enable_language(C)
		if(NOT TARGET ktx)
			add_subdirectory(${ktx_software_SOURCE_DIR} ${ktx_software_BINARY_DIR} EXCLUDE_FROM_ALL)
		endif()
		if(TARGET ktx)
			set(CATTY_HAS_LIBKTX 1)
			set_target_properties(ktx PROPERTIES FOLDER "ThirdParty")
			message(STATUS "Catty: KTX-Software (libktx) enabled")
		else()
			message(WARNING "Catty: KTX-Software sources present but target 'ktx' missing")
		endif()
	else()
		message(STATUS
			"Catty: libktx skipped (no KTX-Software sources). "
			"Set CATTY_KTX_SOURCE_DIR or CATTY_FETCH_LIBKTX=ON when network allows.")
	endif()
	unset(_CATTY_KTX_SRC)
endif()

# Optional OpenImageIO (heavy). Default OFF — Win32 raster uses WIC; enable when deps ready.
option(CATTY_WITH_OPENIMAGEIO "FetchContent OpenImageIO for raster texture IO" OFF)
set(CATTY_HAS_OPENIMAGEIO 0)
if(CATTY_WITH_OPENIMAGEIO)
	message(STATUS "Catty: OpenImageIO requested — wire FetchContent in a follow-up if needed")
endif()

unset(_CATTY_REPO_ROOT)
unset(_CATTY_PUBLIC_HEADERS)
unset(_CATTY_TP)
