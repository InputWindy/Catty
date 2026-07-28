#pragma once

#include "Core/Export.h"

// DLL export / import for plugin CPlatformWindow (CATTY_CPLATFORMWINDOW_MODULE_EXPORTS).

#if defined(CATTY_BUILD_SHARED)
#	if defined(_WIN32) || defined(_WIN64)
#		if defined(CATTY_CPLATFORMWINDOW_MODULE_EXPORTS)
#			define CATTY_CPLATFORMWINDOW_MODULE_API __declspec(dllexport)
#		else
#			define CATTY_CPLATFORMWINDOW_MODULE_API __declspec(dllimport)
#		endif
#	else
#		if defined(CATTY_CPLATFORMWINDOW_MODULE_EXPORTS)
#			define CATTY_CPLATFORMWINDOW_MODULE_API __attribute__((visibility("default")))
#		else
#			define CATTY_CPLATFORMWINDOW_MODULE_API
#		endif
#	endif
#else
#	define CATTY_CPLATFORMWINDOW_MODULE_API
#endif
