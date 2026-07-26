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

# Dear ImGui (sources compiled into Catty; see Catty/CMakeLists.txt)
# Prefer vendored tree (offline-friendly). Fallback to FetchContent when missing.
get_filename_component(_CATTY_DEPS_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(_CATTY_VENDORED_IMGUI "${_CATTY_DEPS_DIR}/ThirdParty/imgui")
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
unset(_CATTY_DEPS_DIR)
unset(_CATTY_VENDORED_IMGUI)

# Vulkan (LunarG SDK via VULKAN_SDK / FindVulkan)
find_package(Vulkan REQUIRED)
message(STATUS "Catty: Vulkan found")
message(STATUS "  Vulkan_INCLUDE_DIRS = ${Vulkan_INCLUDE_DIRS}")
message(STATUS "  Vulkan_LIBRARIES    = ${Vulkan_LIBRARIES}")

find_package(Threads REQUIRED)
