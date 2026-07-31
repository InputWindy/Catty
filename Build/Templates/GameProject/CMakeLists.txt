cmake_minimum_required(VERSION 3.20)

# Standalone Catty game project (generated from Build/Templates/GameProject).
# generateProject.py passes -DCATTY_ENGINE_ROOT=<engine repo>

project({{PROJECT_NAME}}
	VERSION 0.1.0
	DESCRIPTION "{{DESCRIPTION}}"
	LANGUAGES CXX
)

if(NOT CATTY_ENGINE_ROOT)
	message(FATAL_ERROR "CATTY_ENGINE_ROOT is not set. Open/generate via the .cproject file.")
endif()

file(TO_CMAKE_PATH "${CATTY_ENGINE_ROOT}" CATTY_ENGINE_ROOT)
if(NOT EXISTS "${CATTY_ENGINE_ROOT}/Catty/CMakeLists.txt")
	message(FATAL_ERROR "Invalid CATTY_ENGINE_ROOT: ${CATTY_ENGINE_ROOT}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT {{PROJECT_NAME}})

list(APPEND CMAKE_MODULE_PATH "${CATTY_ENGINE_ROOT}/Build/CMake")
include(CattyDirectories)
include(CattyHelpers)

catty_ensure_workspace_dirs()
catty_warn_if_not_intermediate_binary_dir()

option(CATTY_BUILD_SHARED "Build Catty engine as a shared library (DLL)" ON)
# Push Catty::FEditorLayer from the game App (layer lives in Catty.dll under Core/Layer).
option(GAME_WITH_EDITOR "Enable Catty::FEditorLayer overlay (disable for shipping)" ON)

# Enabled plugins come from the sibling .cproject (or -DCATTY_CPROJECT=...).
if(NOT CATTY_CPROJECT)
	set(_CATTY_CPROJECT_CANDIDATE "${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.cproject")
	if(EXISTS "${_CATTY_CPROJECT_CANDIDATE}")
		set(CATTY_CPROJECT "${_CATTY_CPROJECT_CANDIDATE}")
	endif()
	unset(_CATTY_CPROJECT_CANDIDATE)
endif()
if(CATTY_CPROJECT)
	file(TO_CMAKE_PATH "${CATTY_CPROJECT}" CATTY_CPROJECT)
	set(CATTY_CPROJECT "${CATTY_CPROJECT}" CACHE FILEPATH "Game .cproject (enabled Plugins list)" FORCE)
	message(STATUS "Catty: using .cproject for plugins: ${CATTY_CPROJECT}")
endif()

add_subdirectory("${CATTY_ENGINE_ROOT}/Catty" "${CMAKE_BINARY_DIR}/CattyBuild")

set(GAME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(GAME_SOURCE_DIR "${GAME_ROOT}/Source")
set(GAME_SHADERS_DIR "${GAME_ROOT}/Shaders")
set(GAME_PLUGINS_DIR "${GAME_ROOT}/Plugins")
set(GAME_CONFIG_DIR "${GAME_ROOT}/Config")
set(GAME_SCRIPTS_DIR "${GAME_ROOT}/Scripts")

catty_collect_sources(GAME_SOURCES "${GAME_SOURCE_DIR}")
if(GAME_SOURCES STREQUAL "")
	message(FATAL_ERROR "{{PROJECT_NAME}}: no sources under Source/")
endif()

add_executable({{PROJECT_NAME}} ${GAME_SOURCES})
target_include_directories({{PROJECT_NAME}} PRIVATE
	"${GAME_SOURCE_DIR}"
	"${GAME_SOURCE_DIR}/{{PROJECT_NAME}}/Public"
)
target_link_libraries({{PROJECT_NAME}} PRIVATE Catty::Engine Catty::Modules)
target_compile_features({{PROJECT_NAME}} PRIVATE cxx_std_20)
target_compile_definitions({{PROJECT_NAME}} PRIVATE
	$<$<BOOL:${GAME_WITH_EDITOR}>:GAME_WITH_EDITOR=1>
)

if(MSVC)
	target_compile_options({{PROJECT_NAME}} PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8)
else()
	target_compile_options({{PROJECT_NAME}} PRIVATE -Wall -Wextra -Wpedantic)
endif()

catty_set_output_dirs({{PROJECT_NAME}})
catty_source_group_mirrors("${GAME_SOURCE_DIR}" ${GAME_SOURCES})
catty_add_lua_scripts({{PROJECT_NAME}} "${GAME_SCRIPTS_DIR}")
set_target_properties({{PROJECT_NAME}} PROPERTIES
	FOLDER "Games"
	VS_DEBUGGER_WORKING_DIRECTORY "${GAME_ROOT}"
	WIN32_EXECUTABLE $<BOOL:${WIN32}>
)
add_dependencies({{PROJECT_NAME}} Catty CattyModules)

catty_copy_shaders({{PROJECT_NAME}} "${GAME_SHADERS_DIR}" "Project/Shaders")
if(IS_DIRECTORY "${GAME_CONFIG_DIR}")
	add_custom_command(TARGET {{PROJECT_NAME}} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:{{PROJECT_NAME}}>/Config"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${GAME_CONFIG_DIR}"
			"$<TARGET_FILE_DIR:{{PROJECT_NAME}}>/Config"
		COMMENT "Copy project Config (DefaultEngine.ini)"
		VERBATIM
	)
endif()
if(IS_DIRECTORY "${GAME_SCRIPTS_DIR}")
	add_custom_command(TARGET {{PROJECT_NAME}} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:{{PROJECT_NAME}}>/Scripts"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${GAME_SCRIPTS_DIR}"
			"$<TARGET_FILE_DIR:{{PROJECT_NAME}}>/Scripts"
		COMMENT "Copy project Scripts (Lua)"
		VERBATIM
	)
endif()
add_custom_command(TARGET {{PROJECT_NAME}} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:{{PROJECT_NAME}}>/Project/Plugins"
	COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:{{PROJECT_NAME}}>/Engine/Plugins"
	COMMENT "Ensure plugin runtime folders exist"
	VERBATIM
)
if(IS_DIRECTORY "${CATTY_ENGINE_PLUGINS_DIR}")
	add_custom_command(TARGET {{PROJECT_NAME}} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CATTY_ENGINE_PLUGINS_DIR}"
			"$<TARGET_FILE_DIR:{{PROJECT_NAME}}>/Engine/Plugins"
		COMMENT "Copy engine plugins"
		VERBATIM
	)
endif()
if(DEFINED CATTY_ENGINE_FONTS_DIR AND IS_DIRECTORY "${CATTY_ENGINE_FONTS_DIR}")
	add_custom_command(TARGET {{PROJECT_NAME}} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:{{PROJECT_NAME}}>/Engine/Fonts"
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CATTY_ENGINE_FONTS_DIR}"
			"$<TARGET_FILE_DIR:{{PROJECT_NAME}}>/Engine/Fonts"
		COMMENT "Copy engine Fonts"
		VERBATIM
	)
endif()
if(IS_DIRECTORY "${GAME_PLUGINS_DIR}")
	add_custom_command(TARGET {{PROJECT_NAME}} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${GAME_PLUGINS_DIR}"
			"$<TARGET_FILE_DIR:{{PROJECT_NAME}}>/Project/Plugins"
		COMMENT "Copy project plugins"
		VERBATIM
	)
endif()

catty_install_runtime_target({{PROJECT_NAME}})
catty_install_directory("${GAME_SHADERS_DIR}" "Project/Shaders")
catty_install_directory("${GAME_PLUGINS_DIR}" "Project/Plugins")
catty_install_directory("${GAME_CONFIG_DIR}" "Config")
catty_install_directory("${GAME_SCRIPTS_DIR}" "Scripts")

# Packaging is done via project package.bat → Tools/package_ui.py (cmake --install).
