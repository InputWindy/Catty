# Shared CMake helpers for Catty workspace.

function(catty_collect_sources OUT_VAR ROOT_DIR)
	if(NOT IS_DIRECTORY "${ROOT_DIR}")
		set(${OUT_VAR} "" PARENT_SCOPE)
		return()
	endif()

	file(GLOB_RECURSE _sources CONFIGURE_DEPENDS
		"${ROOT_DIR}/*.c"
		"${ROOT_DIR}/*.cc"
		"${ROOT_DIR}/*.cpp"
		"${ROOT_DIR}/*.cxx"
		"${ROOT_DIR}/*.h"
		"${ROOT_DIR}/*.hh"
		"${ROOT_DIR}/*.hpp"
		"${ROOT_DIR}/*.hxx"
		"${ROOT_DIR}/*.inl"
		"${ROOT_DIR}/*.ipp"
	)
	set(${OUT_VAR} "${_sources}" PARENT_SCOPE)
endfunction()

function(catty_source_group_mirrors ROOT_DIR)
	foreach(_file IN LISTS ARGN)
		file(RELATIVE_PATH _rel "${ROOT_DIR}" "${_file}")
		get_filename_component(_dir "${_rel}" DIRECTORY)
		if(_dir STREQUAL "")
			set(_group "Source")
		else()
			string(REPLACE "/" "\\" _group "Source/${_dir}")
		endif()
		source_group("${_group}" FILES "${_file}")
	endforeach()
endfunction()

# Attach .lua files to a target for IDE browsing (not compiled).
function(catty_add_lua_scripts TARGET_NAME SCRIPTS_DIR)
	if(NOT IS_DIRECTORY "${SCRIPTS_DIR}")
		return()
	endif()

	file(GLOB_RECURSE _scripts CONFIGURE_DEPENDS "${SCRIPTS_DIR}/*.lua")
	if(_scripts STREQUAL "")
		return()
	endif()

	target_sources(${TARGET_NAME} PRIVATE ${_scripts})
	set_source_files_properties(${_scripts} PROPERTIES
		HEADER_FILE_ONLY TRUE
		VS_TOOL_OVERRIDE "None"
	)

	foreach(_file IN LISTS _scripts)
		file(RELATIVE_PATH _rel "${SCRIPTS_DIR}" "${_file}")
		get_filename_component(_dir "${_rel}" DIRECTORY)
		if(_dir STREQUAL "")
			set(_group "Scripts")
		else()
			string(REPLACE "/" "\\" _group "Scripts/${_dir}")
		endif()
		source_group("${_group}" FILES "${_file}")
	endforeach()
endfunction()

function(catty_set_output_dirs TARGET_NAME)
	set_target_properties(${TARGET_NAME} PROPERTIES
		RUNTIME_OUTPUT_DIRECTORY "${CATTY_BIN_DIR}"
		LIBRARY_OUTPUT_DIRECTORY "${CATTY_BIN_DIR}"
		ARCHIVE_OUTPUT_DIRECTORY "${CATTY_LIB_DIR}"
		RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CATTY_BIN_DIR}/Debug"
		RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CATTY_BIN_DIR}/Release"
		RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CATTY_BIN_DIR}/RelWithDebInfo"
		RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CATTY_BIN_DIR}/MinSizeRel"
		LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CATTY_BIN_DIR}/Debug"
		LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CATTY_BIN_DIR}/Release"
		LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CATTY_BIN_DIR}/RelWithDebInfo"
		LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CATTY_BIN_DIR}/MinSizeRel"
		ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${CATTY_LIB_DIR}/Debug"
		ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CATTY_LIB_DIR}/Release"
		ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CATTY_LIB_DIR}/RelWithDebInfo"
		ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL "${CATTY_LIB_DIR}/MinSizeRel"
	)
endfunction()

function(catty_copy_shaders TARGET_NAME SHADER_ROOT DEST_SUBDIR)
	if(NOT IS_DIRECTORY "${SHADER_ROOT}")
		return()
	endif()

	add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET_NAME}>/${DEST_SUBDIR}"
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${SHADER_ROOT}" "$<TARGET_FILE_DIR:${TARGET_NAME}>/${DEST_SUBDIR}"
		COMMENT "Copy shaders: ${DEST_SUBDIR}"
		VERBATIM
	)
endfunction()

function(catty_install_runtime_target TARGET_NAME)
	install(FILES "$<TARGET_FILE:${TARGET_NAME}>"
		DESTINATION .
		COMPONENT Runtime
	)
endfunction()

function(catty_install_directory SRC_DIR DEST_SUBDIR)
	if(NOT IS_DIRECTORY "${SRC_DIR}")
		return()
	endif()
	install(DIRECTORY "${SRC_DIR}/"
		DESTINATION "${DEST_SUBDIR}"
		COMPONENT Runtime
		PATTERN ".gitkeep" EXCLUDE
		PATTERN "README.md" EXCLUDE
	)
endfunction()
