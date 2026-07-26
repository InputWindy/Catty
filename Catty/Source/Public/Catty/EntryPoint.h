#pragma once

/**
 * Platform entry point for Catty game executables.
 *
 * Include this header in exactly one .cpp of the game project.
 *
 * Example:
 *   #include <Catty/Catty.h>
 *   #include <Catty/EntryPoint.h>
 *
 *   class FMyGameApp : public Catty::FApp
 *   {
 *   protected:
 *       virtual void Configure(Catty::FEngineConfig& OutConfig) override { ... }
 *       virtual bool PostInitialize() override { return true; }
 *       virtual void FixedUpdate(float FixedDeltaSeconds) override { ... }
 *       virtual void Update(float DeltaSeconds) override { ... }
 *       virtual void LateUpdate(float DeltaSeconds) override { ... }
 *   };
 *
 *   Catty::FApp* Catty::CreateApplication()
 *   {
 *       return new FMyGameApp();
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
