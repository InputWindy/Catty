#pragma once

/**
 * Platform entry point for Catty game executables.
 * Include this header in exactly one .cpp of the game project.
 *
 * FApp ctor assigns Catty::GApp. CATTY_* / CVar / Timer resolve through GApp.
 *
 * On Windows the game is typically linked as a GUI app (WIN32_EXECUTABLE) so no
 * console black box appears; CATTY_LOG lines go to the editor Output Log instead.
 */

#include <Core/App.h>

#include <cstdio>

#if defined(_WIN32)
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <Windows.h>
#endif

namespace
{

int CattyRunMain(int Argc, char** Argv)
{
	(void)Argc;
	(void)Argv;

#if defined(_WIN32)
	// Detach any inherited / debugger console so the OS black box stays hidden.
	FreeConsole();
#endif

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

} // namespace

#if defined(_WIN32)
int WINAPI WinMain(HINSTANCE /*Instance*/, HINSTANCE /*Prev*/, LPSTR /*CmdLine*/, int /*Show*/)
{
	return CattyRunMain(__argc, __argv);
}
#endif

int main(int Argc, char** Argv)
{
	return CattyRunMain(Argc, Argv);
}
