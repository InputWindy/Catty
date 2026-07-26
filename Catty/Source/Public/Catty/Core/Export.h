#pragma once

// DLL export / import (UE-style module boundary).
#if defined(CATTY_BUILD_SHARED)
#	if defined(_WIN32) || defined(_WIN64)
#		if defined(CATTY_EXPORTS)
#			define CATTY_API __declspec(dllexport)
#		else
#			define CATTY_API __declspec(dllimport)
#		endif
#	else
#		define CATTY_API __attribute__((visibility("default")))
#	endif
#else
#	define CATTY_API
#endif
