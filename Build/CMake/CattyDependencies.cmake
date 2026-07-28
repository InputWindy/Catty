# Third-party dependencies for the Catty engine target.

include(FetchContent)

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

# Include as <nlohmann/json.hpp> (vendored single-header under ThirdParty/nlohmann).
set(_CATTY_VENDORED_NLOHMANN_JSON "${_CATTY_REPO_ROOT}/ThirdParty/nlohmann")
if(EXISTS "${_CATTY_VENDORED_NLOHMANN_JSON}/json.hpp")
	set(CATTY_NLOHMANN_JSON_INCLUDE_DIR "${_CATTY_REPO_ROOT}/ThirdParty" CACHE INTERNAL "nlohmann/json include root")
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

# Dear ImGui as a dedicated ThirdParty static library.
# Catty only consumes ImGui headers + links Catty::ImGui; imgui*.cpp stay out of the Catty target.
set(_CATTY_VENDORED_IMGUI "${_CATTY_REPO_ROOT}/ThirdParty/imgui")

if(EXISTS "${_CATTY_VENDORED_IMGUI}/imgui.cpp")
	set(CATTY_IMGUI_SOURCE_DIR "${_CATTY_VENDORED_IMGUI}" CACHE INTERNAL "Dear ImGui source directory")
	message(STATUS "Catty: ImGui (vendored) at ${CATTY_IMGUI_SOURCE_DIR}")
else()
	FetchContent_Declare(
		imgui
		GIT_REPOSITORY https://github.com/ocornut/imgui.git
		GIT_TAG v1.91.9
		GIT_SHALLOW TRUE
	)
	FetchContent_GetProperties(imgui)
	if(NOT imgui_POPULATED)
		FetchContent_Populate(imgui)
	endif()
	set(CATTY_IMGUI_SOURCE_DIR "${imgui_SOURCE_DIR}" CACHE INTERNAL "Dear ImGui source directory")
	message(STATUS "Catty: ImGui (FetchContent) at ${CATTY_IMGUI_SOURCE_DIR}")
endif()

set(CATTY_IMGUI_SOURCES
	"${CATTY_IMGUI_SOURCE_DIR}/imgui.cpp"
	"${CATTY_IMGUI_SOURCE_DIR}/imgui_demo.cpp"
	"${CATTY_IMGUI_SOURCE_DIR}/imgui_draw.cpp"
	"${CATTY_IMGUI_SOURCE_DIR}/imgui_tables.cpp"
	"${CATTY_IMGUI_SOURCE_DIR}/imgui_widgets.cpp"
	"${CATTY_IMGUI_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
	"${CATTY_IMGUI_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp"
)

# STATIC ThirdParty lib (own .vcxproj under ThirdParty/). Avoid OBJECT — VS would
# nest imgui*.obj under Catty's "Object Libraries" filter.
add_library(imgui STATIC ${CATTY_IMGUI_SOURCES})
add_library(Catty::ImGui ALIAS imgui)

target_include_directories(imgui
	PUBLIC
		"${CATTY_IMGUI_SOURCE_DIR}"
		"${CATTY_IMGUI_SOURCE_DIR}/backends"
		# IMGUI_USER_CONFIG resolves to UI/ImGuiConfig.h under this include root.
		"${_CATTY_PUBLIC_HEADERS}"
)

target_compile_definitions(imgui
	PUBLIC
		IMGUI_USER_CONFIG="UI/ImGuiConfig.h"
		CATTY_WITH_IMGUI=1
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
endif()

set_target_properties(imgui PROPERTIES
	FOLDER "ThirdParty"
	POSITION_INDEPENDENT_CODE ON
)

source_group(TREE "${CATTY_IMGUI_SOURCE_DIR}" PREFIX "imgui" FILES ${CATTY_IMGUI_SOURCES})

# ---------------------------------------------------------------------------
# Lua 5.4 (static) + sol2 (header-only bindings)
# ---------------------------------------------------------------------------
set(_CATTY_VENDORED_LUA "${_CATTY_REPO_ROOT}/ThirdParty/lua")
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
	FetchContent_GetProperties(lua_src)
	if(NOT lua_src_POPULATED)
		FetchContent_Populate(lua_src)
	endif()
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

set(_CATTY_VENDORED_SOL2 "${_CATTY_REPO_ROOT}/ThirdParty/sol2")
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
	FetchContent_GetProperties(sol2)
	if(NOT sol2_POPULATED)
		FetchContent_Populate(sol2)
	endif()
	set(CATTY_SOL2_INCLUDE_DIR "${sol2_SOURCE_DIR}/include" CACHE INTERNAL "sol2 include directory")
	message(STATUS "Catty: sol2 (FetchContent) at ${CATTY_SOL2_INCLUDE_DIR}")
endif()

unset(_CATTY_VENDORED_LUA)
unset(_CATTY_VENDORED_SOL2)

# ---------------------------------------------------------------------------
# refl-cpp (header-only) — optional / legacy; FObject reflection uses ObjectReflect.h + codegen.
# ---------------------------------------------------------------------------
set(_CATTY_VENDORED_REFL "${_CATTY_REPO_ROOT}/ThirdParty/refl-cpp")
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
	FetchContent_GetProperties(refl_cpp)
	if(NOT refl_cpp_POPULATED)
		FetchContent_Populate(refl_cpp)
	endif()
	set(CATTY_REFL_INCLUDE_DIR "${refl_cpp_SOURCE_DIR}/include" CACHE INTERNAL "refl-cpp include directory")
	message(STATUS "Catty: refl-cpp (FetchContent) at ${CATTY_REFL_INCLUDE_DIR}")
endif()
unset(_CATTY_VENDORED_REFL)

unset(_CATTY_REPO_ROOT)
unset(_CATTY_PUBLIC_HEADERS)
unset(_CATTY_VENDORED_IMGUI)
