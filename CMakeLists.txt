cmake_minimum_required(VERSION 3.20)

project(CattyWorkspace
	VERSION 0.1.0
	DESCRIPTION "Catty engine workspace (Engine DLL + Game EXE)"
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Test0)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Build/CMake")
include(CattyDirectories)
include(CattyHelpers)

catty_ensure_workspace_dirs()
catty_warn_if_not_intermediate_binary_dir()

option(CATTY_BUILD_SHARED "Build Catty engine as a shared library (DLL)" ON)

add_subdirectory(Catty)
add_subdirectory(Test0)

# Clean per-platform shipping tree: Packaged/<Platform>/
add_custom_target(Package
	COMMAND ${CMAKE_COMMAND} -E rm -rf "${CATTY_PACKAGED_DIR}"
	COMMAND ${CMAKE_COMMAND} -E make_directory "${CATTY_PACKAGED_DIR}"
	COMMAND ${CMAKE_COMMAND}
		--install "${CMAKE_BINARY_DIR}"
		--prefix "${CATTY_PACKAGED_DIR}"
		--config $<CONFIG>
		--component Runtime
	COMMENT "Packaging ${CATTY_PLATFORM_NAME} → Packaged/${CATTY_PLATFORM_NAME}"
	VERBATIM
)
set_target_properties(Package PROPERTIES FOLDER "Packaging")
add_dependencies(Package Test0 Catty)

message(STATUS "=== Catty Workspace (UE-style dirs) ===")
message(STATUS "  Platform     : ${CATTY_PLATFORM_NAME}")
message(STATUS "  Intermediate : ${CATTY_INTERMEDIATE_DIR}  (cmake -B)")
message(STATUS "  Binaries     : ${CATTY_BINARIES_DIR}/<Config>")
message(STATUS "  Cached       : ${CATTY_CACHED_DIR}")
message(STATUS "  Saved        : ${CATTY_SAVED_DIR}")
message(STATUS "  Packaged     : ${CATTY_PACKAGED_DIR}  (target Package)")
message(STATUS "Configure:")
message(STATUS "  cmake -S . -B Intermediate -G \"Visual Studio 17 2022\" -A x64")
message(STATUS "  cmake --build Intermediate --config Debug")
message(STATUS "  cmake --build Intermediate --config Release --target Package")
