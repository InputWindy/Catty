#pragma once

// DLL export for the Sample plugin module (CMake: MAHO_SAMPLE_MODULE_EXPORTS).
#if defined(MAHO_BUILD_SHARED)
#	if defined(_WIN32) || defined(_WIN64)
#		if defined(MAHO_SAMPLE_MODULE_EXPORTS)
#			define MAHO_SAMPLE_API __declspec(dllexport)
#		else
#			define MAHO_SAMPLE_API __declspec(dllimport)
#		endif
#	else
#		if defined(MAHO_SAMPLE_MODULE_EXPORTS)
#			define MAHO_SAMPLE_API __attribute__((visibility("default")))
#		else
#			define MAHO_SAMPLE_API
#		endif
#	endif
#else
#	define MAHO_SAMPLE_API
#endif
