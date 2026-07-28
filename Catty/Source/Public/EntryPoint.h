#pragma once

/**
 * Platform entry point for Catty game executables.
 * Include this header in exactly one .cpp of the game project.
 *
 * FApp ctor assigns Catty::GApp. CATTY_* / CVar / Timer resolve through GApp.
 */

#include <Core/App.h>

#include <cstdio>

int main(int Argc, char** Argv)
{
	(void)Argc;
	(void)Argv;

	Catty::FApp* App = Catty::CreateApplication();
	if (!App)
	{
		std::fprintf(stderr, "CreateApplication returned null\n");
		return 1;
	}

	App->Run();
	delete App;
	return 0;
}
