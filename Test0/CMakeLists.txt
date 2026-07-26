cmake_minimum_required(VERSION 3.20)

set(TEST0_ROOT "${CMAKE_CURRENT_SOURCE_DIR}")
set(TEST0_SOURCE_DIR "${TEST0_ROOT}/Source")
set(TEST0_SHADERS_DIR "${TEST0_ROOT}/Shaders")
set(TEST0_PLUGINS_DIR "${TEST0_ROOT}/Plugins")

catty_collect_sources(TEST0_SOURCES "${TEST0_SOURCE_DIR}")

if(TEST0_SOURCES STREQUAL "")
	message(FATAL_ERROR "Test0: no sources found under ${TEST0_SOURCE_DIR}")
endif()

add_executable(Test0 ${TEST0_SOURCES})

target_include_directories(Test0
	PRIVATE
		"${TEST0_SOURCE_DIR}"
)

target_link_libraries(Test0
	PRIVATE
		Catty::Engine
)

target_compile_features(Test0 PRIVATE cxx_std_20)

if(MSVC)
	target_compile_options(Test0 PRIVATE /W4 /permissive- /Zc:preprocessor /utf-8)
else()
	target_compile_options(Test0 PRIVATE -Wall -Wextra -Wpedantic)
endif()

catty_set_output_dirs(Test0)
catty_source_group_mirrors("${TEST0_SOURCE_DIR}" ${TEST0_SOURCES})

set_target_properties(Test0 PROPERTIES
	FOLDER "Games"
	VS_DEBUGGER_WORKING_DIRECTORY "${CATTY_WORKSPACE_ROOT}"
)

add_dependencies(Test0 Catty)

catty_copy_shaders(Test0 "${TEST0_SHADERS_DIR}" "Project/Shaders")

add_custom_command(TARGET Test0 POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Test0>/Project/Plugins"
	COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:Test0>/Engine/Plugins"
	COMMENT "Ensure plugin runtime folders exist"
	VERBATIM
)

if(IS_DIRECTORY "${CATTY_ENGINE_PLUGINS_DIR}")
	add_custom_command(TARGET Test0 POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${CATTY_ENGINE_PLUGINS_DIR}"
			"$<TARGET_FILE_DIR:Test0>/Engine/Plugins"
		COMMENT "Copy engine plugins"
		VERBATIM
	)
endif()

if(IS_DIRECTORY "${TEST0_PLUGINS_DIR}")
	add_custom_command(TARGET Test0 POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory
			"${TEST0_PLUGINS_DIR}"
			"$<TARGET_FILE_DIR:Test0>/Project/Plugins"
		COMMENT "Copy project plugins"
		VERBATIM
	)
endif()

catty_install_runtime_target(Test0)
catty_install_directory("${TEST0_SHADERS_DIR}" "Project/Shaders")
catty_install_directory("${TEST0_PLUGINS_DIR}" "Project/Plugins")
