#pragma once

/**
 * Platform entry point for Catty game executables.
 *
 * Include this header in exactly one .cpp of the game project.
 *
 * Example (Test0):
 *   #include <Catty/Catty.h>
 *   #include <Catty/EntryPoint.h>
 *
 *   class FTest0App : public Catty::FApp { ... };
 *
 *   Catty::FApp* Catty::CreateApplication()
 *   {
 *       return new FTest0App();
 *   }
 */

#include "Catty/Core/App.h"

#include <iostream>

int main(int Argc, char** Argv)
{
	(void)Argc;
	(void)Argv;

	Catty::FApp* App = Catty::CreateApplication();
	if (!App)
	{
		std::cerr << "[Catty] CreateApplication returned null\n";
		return 1;
	}

	App->Run();
	delete App;
	return 0;
}
