cmake_minimum_required(VERSION 3.20)

set(CATTY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(CATTY_SOURCE_DIR "${CATTY_ROOT}/Source")
set(CATTY_PUBLIC_DIR "${CATTY_SOURCE_DIR}/Public")
set(CATTY_PRIVATE_DIR "${CATTY_SOURCE_DIR}/Private")
set(CATTY_SHADERS_DIR "${CATTY_ROOT}/Shaders")
set(CATTY_PLUGINS_DIR "${CATTY_ROOT}/Plugins")

# Resolve Build/CMake helpers when Catty is added from engine workspace or external .cproject.
if(NOT DEFINED CATTY_ENGINE_BUILD_CMAKE_DIR)
	get_filename_component(CATTY_ENGINE_BUILD_CMAKE_DIR "${CATTY_ROOT}/../Build/CMake" ABSOLUTE)
endif()
list(APPEND CMAKE_MODULE_PATH "${CATTY_ENGINE_BUILD_CMAKE_DIR}")
include(CattyDependencies)

catty_collect_sources(CATTY_SOURCES "${CATTY_SOURCE_DIR}")

if(CATTY_SOURCES STREQUAL "")
	message(FATAL_ERROR "Catty: no sources found under ${CATTY_SOURCE_DIR}")
endif()

if(CATTY_BUILD_SHARED)
	add_library(Catty SHARED ${CATTY_SOURCES})
	target_compile_definitions(Catty
		PUBLIC  CATTY_BUILD_SHARED=1
		PRIVATE CATTY_EXPORTS=1
	)
else()
	add_library(Catty STATIC ${CATTY_SOURCES})
endif()

add_library(Catty::Engine ALIAS Catty)

target_include_directories(Catty
	PUBLIC
		"${CATTY_PUBLIC_DIR}"
	PRIVATE
		"${CATTY_PRIVATE_DIR}"
)

# PUBLIC: game code uses CATTY_* macros that expand to spdlog calls.
# Header-only avoids duplicating a static spdlog into both Catty.dll and the game EXE.
target_link_libraries(Catty
	PUBLIC
		spdlog::spdlog_header_only
		Vulkan::Vulkan
	PRIVATE
		glfw
		Threads::Threads
)

target_compile_definitions(Catty
	PUBLIC
		CATTY_WITH_VULKAN=1
		CATTY_PLATFORM_${CATTY_PLATFORM_NAME}=1
)

if(WIN32)
	target_compile_definitions(Catty PRIVATE VK_USE_PLATFORM_WIN32_KHR=1)
endif()

target_compile_features(Catty PUBLIC cxx_std_20)

if(MSVC)
	target_compile_options(Catty PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8)
	set_target_properties(Catty PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
else()
	target_compile_options(Catty PRIVATE -Wall -Wextra -Wpedantic)
	if(CATTY_BUILD_SHARED)
		set_target_properties(Catty PROPERTIES
			CXX_VISIBILITY_PRESET hidden
			VISIBILITY_INLINES_HIDDEN ON
		)
	endif()
endif()

catty_set_output_dirs(Catty)
catty_source_group_mirrors("${CATTY_SOURCE_DIR}" ${CATTY_SOURCES})
set_target_properties(Catty PROPERTIES
	FOLDER "Engine"
	OUTPUT_NAME "Catty"
)

catty_copy_shaders(Catty "${CATTY_SHADERS_DIR}" "Engine/Shaders")

# Preserve -DCATTY_ENGINE_ROOT from game projects (repo root). Never overwrite with Catty/.
if(NOT CATTY_ENGINE_ROOT)
	get_filename_component(CATTY_ENGINE_ROOT "${CATTY_ROOT}/.." ABSOLUTE)
endif()
set(CATTY_ENGINE_SHADERS_DIR "${CATTY_SHADERS_DIR}" CACHE INTERNAL "Catty engine shaders")
set(CATTY_ENGINE_PLUGINS_DIR "${CATTY_PLUGINS_DIR}" CACHE INTERNAL "Catty engine plugins")

catty_install_runtime_target(Catty)
catty_install_directory("${CATTY_SHADERS_DIR}" "Engine/Shaders")
catty_install_directory("${CATTY_PLUGINS_DIR}" "Engine/Plugins")
