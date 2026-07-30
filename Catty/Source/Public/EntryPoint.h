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

#include <Core/Application/App.h>
#include <Core/System/Fatal.h>

#include <cstdio>
#include <exception>
#include <string>

namespace
{

int CattyMain(int Argc, char** Argv)
{
	(void)Argc;
	(void)Argv;

	Catty::InstallFatalHandlers();

	Catty::FApp* App = nullptr;
	try
	{
		App = Catty::CreateApplication();
		if (!App)
		{
			Catty::ReportFatal("CreateApplication returned null");
		}

		App->Run();
		delete App;
		App = nullptr;
		return 0;
	}
	catch (const std::exception& Exception)
	{
		delete App;
		App = nullptr;
		const std::string Message = std::string("Unhandled exception: ") + Exception.what();
		Catty::ReportFatal(Message.c_str());
	}
	catch (...)
	{
		delete App;
		App = nullptr;
		Catty::ReportFatal("Unhandled unknown exception");
	}
}

} // namespace

#if defined(_WIN32)
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <Windows.h>
int WINAPI WinMain(HINSTANCE /*Instance*/, HINSTANCE /*Prev*/, LPSTR /*CmdLine*/, int /*Show*/)
{
	// Detach any inherited / debugger console so the OS black box stays hidden.
	FreeConsole();

	return CattyMain(__argc, __argv);
}
#endif

int main(int Argc, char** Argv)
{
	return CattyMain(Argc, Argv);
}
