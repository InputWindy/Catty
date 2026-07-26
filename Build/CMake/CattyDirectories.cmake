# UE-style workspace directories + platform id.
# Intermediate  = compile/link intermediates + CMake/VS project files (-B Intermediate)
# Binaries      = daily-run executables / DLLs (per platform)
# Cached        = derived data (shader cache, DDC-like) — regenerable
# Saved         = logs / config / crashes / screenshots — user & session data
# Packaged      = clean shipping tree (exe + required resources only)

# Prefer an explicit workspace root (engine Build/CMakeLists sets this to repo root).
# Game projects leave it unset so CMAKE_SOURCE_DIR (the .cproject folder) is used.
if(NOT DEFINED CATTY_WORKSPACE_ROOT OR "${CATTY_WORKSPACE_ROOT}" STREQUAL "")
	set(CATTY_WORKSPACE_ROOT "${CMAKE_SOURCE_DIR}")
endif()
get_filename_component(CATTY_WORKSPACE_ROOT "${CATTY_WORKSPACE_ROOT}" ABSOLUTE)

if(WIN32)
	set(CATTY_PLATFORM_NAME "Win64")
	set(CATTY_PLATFORM_WINDOWS TRUE)
elseif(APPLE)
	set(CATTY_PLATFORM_NAME "Mac")
	set(CATTY_PLATFORM_MAC TRUE)
else()
	set(CATTY_PLATFORM_NAME "Linux")
	set(CATTY_PLATFORM_LINUX TRUE)
endif()

set(CATTY_INTERMEDIATE_DIR "${CATTY_WORKSPACE_ROOT}/Intermediate")
set(CATTY_BINARIES_DIR     "${CATTY_WORKSPACE_ROOT}/Binaries/${CATTY_PLATFORM_NAME}")
set(CATTY_CACHED_DIR       "${CATTY_WORKSPACE_ROOT}/Cached")
set(CATTY_SAVED_DIR        "${CATTY_WORKSPACE_ROOT}/Saved")
set(CATTY_PACKAGED_DIR     "${CATTY_WORKSPACE_ROOT}/Packaged/${CATTY_PLATFORM_NAME}")

set(CATTY_BIN_DIR "${CATTY_BINARIES_DIR}")
set(CATTY_LIB_DIR "${CATTY_INTERMEDIATE_DIR}/Lib/${CATTY_PLATFORM_NAME}")

function(catty_ensure_workspace_dirs)
	foreach(_dir
		"${CATTY_BINARIES_DIR}"
		"${CATTY_CACHED_DIR}/Shaders"
		"${CATTY_CACHED_DIR}/DerivedData"
		"${CATTY_SAVED_DIR}/Logs"
		"${CATTY_SAVED_DIR}/Config"
		"${CATTY_SAVED_DIR}/Crashes"
		"${CATTY_SAVED_DIR}/Screenshots"
		"${CATTY_PACKAGED_DIR}"
	)
		file(MAKE_DIRECTORY "${_dir}")
	endforeach()
endfunction()

function(catty_warn_if_not_intermediate_binary_dir)
	file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}" _bin)
	file(TO_CMAKE_PATH "${CATTY_INTERMEDIATE_DIR}" _want)
	if(NOT _bin STREQUAL _want)
		message(WARNING
			"CMAKE_BINARY_DIR is:\n  ${_bin}\n"
			"UE-style layout expects Intermediate as the CMake binary dir:\n"
			"  generateProject.bat\n"
			"  (or cmake -S Build -B Intermediate ...)"
		)
	endif()
endfunction()
