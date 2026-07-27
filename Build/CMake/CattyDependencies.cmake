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

# OBJECT library: sources live under ThirdParty in the .sln; Catty links the objects
# (all TUs included) without compiling imgui*.cpp into the Catty target source list.
add_library(imgui OBJECT ${CATTY_IMGUI_SOURCES})
add_library(Catty::ImGui ALIAS imgui)

target_include_directories(imgui
	PUBLIC
		"${CATTY_IMGUI_SOURCE_DIR}"
		"${CATTY_IMGUI_SOURCE_DIR}/backends"
		# IMGUI_USER_CONFIG resolves to Catty/UI/ImGuiConfig.h under this include root.
		"${_CATTY_PUBLIC_HEADERS}"
)

target_compile_definitions(imgui
	PUBLIC
		IMGUI_USER_CONFIG="Catty/UI/ImGuiConfig.h"
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

unset(_CATTY_REPO_ROOT)
unset(_CATTY_PUBLIC_HEADERS)
unset(_CATTY_VENDORED_IMGUI)
