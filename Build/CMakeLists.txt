cmake_minimum_required(VERSION 3.20)

# Engine workspace entry (kept under Build/ so repo root stays clean).
# generateProject.bat → cmake -S Build -B Intermediate

get_filename_component(CATTY_WORKSPACE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

project(CattyWorkspace
	VERSION 0.1.0
	DESCRIPTION "Catty engine workspace (Engine DLL + Game EXE)"
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")
include(CattyDirectories)
include(CattyHelpers)

catty_ensure_workspace_dirs()
catty_warn_if_not_intermediate_binary_dir()

option(CATTY_BUILD_SHARED "Build Catty engine as a shared library (DLL)" ON)

add_subdirectory("${CATTY_WORKSPACE_ROOT}/Catty" "${CMAKE_BINARY_DIR}/Catty")

# Optional in-tree sample game (may be absent when using external .cproject games).
if(EXISTS "${CATTY_WORKSPACE_ROOT}/Test0/CMakeLists.txt")
	add_subdirectory("${CATTY_WORKSPACE_ROOT}/Test0" "${CMAKE_BINARY_DIR}/Test0")
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Test0)
else()
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Catty)
endif()

# Packaging is done via Tools/package_ui.py (cmake --install), not a VS solution target.

message(STATUS "=== Catty Workspace (UE-style dirs) ===")
message(STATUS "  Workspace   : ${CATTY_WORKSPACE_ROOT}")
message(STATUS "  Platform    : ${CATTY_PLATFORM_NAME}")
message(STATUS "  Intermediate: ${CATTY_INTERMEDIATE_DIR}")
message(STATUS "  Binaries    : ${CATTY_BINARIES_DIR}/<Config>")
message(STATUS "  Packaged    : ${CATTY_PACKAGED_DIR}")
message(STATUS "Configure via: generateProject.bat")
message(STATUS "Package via  : package.bat")
