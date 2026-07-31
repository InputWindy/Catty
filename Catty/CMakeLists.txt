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
include(CattyHelpers)

set(CATTY_GENERATED_DIR "${CATTY_SOURCE_DIR}/Generated")

# Engine repo root (Tools/python + object_reflect_codegen.py).
get_filename_component(_CATTY_ENGINE_ROOT_FOR_CODEGEN "${CATTY_ROOT}/.." ABSOLUTE)
set(_CATTY_OBJECT_REFLECT_CODEGEN_PY "${_CATTY_ENGINE_ROOT_FOR_CODEGEN}/Tools/object_reflect_codegen.py")
set(_CATTY_LOCAL_PYTHON "${_CATTY_ENGINE_ROOT_FOR_CODEGEN}/Tools/python/python.exe")
set(_CATTY_GEN_OBJECT_H "${CATTY_GENERATED_DIR}/ObjectReflectTypes.gen.h")
set(_CATTY_GEN_OBJECT_CPP "${CATTY_GENERATED_DIR}/ObjectReflectTypes.gen.cpp")
set(_CATTY_GEN_LUA_H "${CATTY_GENERATED_DIR}/LuaReflectBindings.gen.h")
set(_CATTY_GEN_LUA_CPP "${CATTY_GENERATED_DIR}/LuaReflectBindings.gen.cpp")
set(_CATTY_GEN_RESOURCE_H "${CATTY_GENERATED_DIR}/ResourceTypes.gen.h")
set(_CATTY_GEN_RESOURCE_CPP "${CATTY_GENERATED_DIR}/ResourceTypes.gen.cpp")

# Build-time FObject reflection codegen.
if(NOT EXISTS "${_CATTY_LOCAL_PYTHON}")
	message(FATAL_ERROR
		"Catty: engine-local Python missing:\n  ${_CATTY_LOCAL_PYTHON}\n"
		"Run setup.bat in the Catty engine root, then re-configure.")
endif()
if(NOT EXISTS "${_CATTY_OBJECT_REFLECT_CODEGEN_PY}")
	message(FATAL_ERROR "Catty: missing ${_CATTY_OBJECT_REFLECT_CODEGEN_PY}")
endif()

execute_process(
	COMMAND "${_CATTY_LOCAL_PYTHON}" "${_CATTY_OBJECT_REFLECT_CODEGEN_PY}"
	WORKING_DIRECTORY "${_CATTY_ENGINE_ROOT_FOR_CODEGEN}"
	RESULT_VARIABLE _CATTY_CODEGEN_RC
	OUTPUT_VARIABLE _CATTY_CODEGEN_OUT
	ERROR_VARIABLE _CATTY_CODEGEN_ERR
)
if(NOT _CATTY_CODEGEN_RC EQUAL 0)
	message(FATAL_ERROR
		"Catty object reflect codegen failed (exit ${_CATTY_CODEGEN_RC}):\n"
		"${_CATTY_CODEGEN_OUT}${_CATTY_CODEGEN_ERR}")
endif()
message(STATUS "Catty: object reflect codegen OK")

catty_collect_sources(CATTY_SOURCES "${CATTY_SOURCE_DIR}")

# Drop anything under Source/Generated from the glob — added explicitly below.
set(_CATTY_SOURCES_FILTERED "")
foreach(_src IN LISTS CATTY_SOURCES)
	string(FIND "${_src}" "/Generated/" _catty_gen_pos)
	string(FIND "${_src}" "\\Generated\\" _catty_gen_pos_win)
	if(_catty_gen_pos EQUAL -1 AND _catty_gen_pos_win EQUAL -1)
		list(APPEND _CATTY_SOURCES_FILTERED "${_src}")
	endif()
endforeach()
set(CATTY_SOURCES "${_CATTY_SOURCES_FILTERED}")

list(APPEND CATTY_SOURCES
	"${_CATTY_GEN_OBJECT_CPP}"
	"${_CATTY_GEN_OBJECT_H}"
	"${_CATTY_GEN_LUA_CPP}"
	"${_CATTY_GEN_LUA_H}"
	"${_CATTY_GEN_RESOURCE_CPP}"
	"${_CATTY_GEN_RESOURCE_H}"
)
list(REMOVE_DUPLICATES CATTY_SOURCES)

if(CATTY_SOURCES STREQUAL "")
	message(FATAL_ERROR "Catty: no sources found under ${CATTY_SOURCE_DIR}")
endif()

if(NOT TARGET imgui)
	message(FATAL_ERROR "Catty: imgui ThirdParty target was not created (CattyDependencies.cmake)")
endif()
if(NOT TARGET lua)
	message(FATAL_ERROR "Catty: lua ThirdParty target was not created (CattyDependencies.cmake)")
endif()
if(NOT CATTY_SOL2_INCLUDE_DIR)
	message(FATAL_ERROR "Catty: sol2 include dir was not set (CattyDependencies.cmake)")
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

# Regenerate FObject reflection tables on every Catty build.
add_custom_command(TARGET Catty PRE_BUILD
	COMMAND "${_CATTY_LOCAL_PYTHON}" "${_CATTY_OBJECT_REFLECT_CODEGEN_PY}"
	WORKING_DIRECTORY "${_CATTY_ENGINE_ROOT_FOR_CODEGEN}"
	COMMENT "Catty: regenerate ObjectReflectTypes"
	VERBATIM
)

if(NOT CATTY_VMA_INCLUDE_DIR)
	message(FATAL_ERROR "Catty: VMA include dir was not set (CattyDependencies.cmake)")
endif()

target_include_directories(Catty
	PUBLIC
		"${CATTY_PUBLIC_DIR}"
		"${CATTY_GENERATED_DIR}"
	PRIVATE
		"${CATTY_PRIVATE_DIR}"
		"${CATTY_NLOHMANN_JSON_INCLUDE_DIR}"
		"${CATTY_SOL2_INCLUDE_DIR}"
		"${CATTY_VMA_INCLUDE_DIR}"
)

# PUBLIC: game code uses CATTY_* macros that expand to spdlog calls.
# Header-only avoids duplicating a static spdlog into both Catty.dll and the game EXE.
# ImGui: separate ThirdParty static lib; Catty.dll consumes headers + links all TUs (WHOLEARCHIVE).
target_link_libraries(Catty
	PUBLIC
		spdlog::spdlog_header_only
		Vulkan::Vulkan
	PRIVATE
		glfw
		Threads::Threads
		lua
)

if(CATTY_HAS_LIBKTX)
	target_link_libraries(Catty PRIVATE ktx)
	target_compile_definitions(Catty PRIVATE CATTY_WITH_LIBKTX=1)
endif()
if(CATTY_HAS_OPENIMAGEIO)
	target_compile_definitions(Catty PRIVATE CATTY_WITH_OPENIMAGEIO=1)
endif()

# Re-export ImGui include paths / defs so game TUs can #include <imgui.h> against Catty.dll.
target_include_directories(Catty
	PUBLIC
		$<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>
)
target_compile_definitions(Catty
	PUBLIC
		$<TARGET_PROPERTY:imgui,INTERFACE_COMPILE_DEFINITIONS>
)

# Pull every imgui TU into Catty.dll (demo / widgets otherwise dropped by the linker).
# Do not also target_link_libraries(imgui) on MSVC shared — that + WHOLEARCHIVE duplicates.
add_dependencies(Catty imgui)
if(CATTY_BUILD_SHARED)
	if(MSVC)
		target_link_options(Catty PRIVATE "/WHOLEARCHIVE:$<TARGET_FILE:imgui>")
	else()
		target_link_libraries(Catty PRIVATE
			"-Wl,--whole-archive"
			imgui
			"-Wl,--no-whole-archive"
		)
	endif()
else()
	target_link_libraries(Catty PRIVATE imgui)
endif()

target_compile_definitions(Catty
	PUBLIC
		CATTY_WITH_VULKAN=1
		CATTY_WITH_LUA=1
		CATTY_WITH_REFLECT=1
		CATTY_PLATFORM_${CATTY_PLATFORM_NAME}=1
)

if(WIN32)
	target_compile_definitions(Catty PRIVATE VK_USE_PLATFORM_WIN32_KHR=1)
	target_link_libraries(Catty PRIVATE winhttp)
endif()

# Absolute engine root for locating Tools/AgentBridge at runtime (dev builds).
file(TO_CMAKE_PATH "${_CATTY_ENGINE_ROOT_FOR_CODEGEN}" _CATTY_ENGINE_ROOT_PATH)
string(REPLACE "\\" "/" _CATTY_ENGINE_ROOT_FWD "${_CATTY_ENGINE_ROOT_PATH}")
target_compile_definitions(Catty PRIVATE "CATTY_ENGINE_ROOT=\"${_CATTY_ENGINE_ROOT_FWD}\"")

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
if(DEFINED CATTY_ENGINE_FONTS_DIR AND IS_DIRECTORY "${CATTY_ENGINE_FONTS_DIR}")
	add_custom_command(TARGET Catty POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Catty>/Engine/Fonts"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CATTY_ENGINE_FONTS_DIR}"
			"$<TARGET_FILE_DIR:Catty>/Engine/Fonts"
		COMMENT "Copy engine Fonts"
		VERBATIM
	)
	catty_install_directory("${CATTY_ENGINE_FONTS_DIR}" "Engine/Fonts")
endif()

# Preserve -DCATTY_ENGINE_ROOT from game projects (repo root). Never overwrite with Catty/.
if(NOT CATTY_ENGINE_ROOT)
	get_filename_component(CATTY_ENGINE_ROOT "${CATTY_ROOT}/.." ABSOLUTE)
endif()
set(CATTY_ENGINE_SHADERS_DIR "${CATTY_SHADERS_DIR}" CACHE INTERNAL "Catty engine shaders")
set(CATTY_ENGINE_PLUGINS_DIR "${CATTY_PLUGINS_DIR}" CACHE INTERNAL "Catty engine plugins")

catty_install_runtime_target(Catty)
catty_install_directory("${CATTY_SHADERS_DIR}" "Engine/Shaders")
catty_install_directory("${CATTY_PLUGINS_DIR}" "Engine/Plugins")

set(_CATTY_SCAN_PLUGINS_PY "${_CATTY_ENGINE_ROOT_FOR_CODEGEN}/Tools/scan_plugins.py")
if(CATTY_CPROJECT)
	catty_add_plugin_modules(
		CPROJECT "${CATTY_CPROJECT}"
		ENGINE_TARGET Catty
		SCAN_SCRIPT "${_CATTY_SCAN_PLUGINS_PY}"
		PYTHON_EXE "${_CATTY_LOCAL_PYTHON}"
	)
else()
	catty_add_plugin_modules(
		PLUGINS_DIR "${CATTY_PLUGINS_DIR}"
		ENGINE_TARGET Catty
		SCAN_SCRIPT "${_CATTY_SCAN_PLUGINS_PY}"
		PYTHON_EXE "${_CATTY_LOCAL_PYTHON}"
	)
endif()
