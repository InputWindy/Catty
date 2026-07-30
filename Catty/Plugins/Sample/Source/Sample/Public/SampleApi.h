#pragma once

// DLL export for the Sample plugin module (CMake: CATTY_SAMPLE_MODULE_EXPORTS).
#if defined(CATTY_BUILD_SHARED)
#	if defined(_WIN32) || defined(_WIN64)
#		if defined(CATTY_SAMPLE_MODULE_EXPORTS)
#			define CATTY_SAMPLE_API __declspec(dllexport)
#		else
#			define CATTY_SAMPLE_API __declspec(dllimport)
#		endif
#	else
#		if defined(CATTY_SAMPLE_MODULE_EXPORTS)
#			define CATTY_SAMPLE_API __attribute__((visibility("default")))
#		else
#			define CATTY_SAMPLE_API
#		endif
#	endif
#else
#	define CATTY_SAMPLE_API
#endif
