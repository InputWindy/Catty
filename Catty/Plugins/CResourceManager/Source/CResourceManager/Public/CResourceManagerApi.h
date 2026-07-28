#pragma once

#include "Core/Export.h"

// DLL export / import for plugin CResourceManager (CATTY_CRESOURCEMANAGER_MODULE_EXPORTS).

#if defined(CATTY_BUILD_SHARED)
#	if defined(_WIN32) || defined(_WIN64)
#		if defined(CATTY_CRESOURCEMANAGER_MODULE_EXPORTS)
#			define CATTY_CRESOURCEMANAGER_MODULE_API __declspec(dllexport)
#		else
#			define CATTY_CRESOURCEMANAGER_MODULE_API __declspec(dllimport)
#		endif
#	else
#		if defined(CATTY_CRESOURCEMANAGER_MODULE_EXPORTS)
#			define CATTY_CRESOURCEMANAGER_MODULE_API __attribute__((visibility("default")))
#		else
#			define CATTY_CRESOURCEMANAGER_MODULE_API
#		endif
#	endif
#else
#	define CATTY_CRESOURCEMANAGER_MODULE_API
#endif
