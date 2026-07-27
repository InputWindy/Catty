#pragma once

/**
 * Platform entry point for Catty game executables.
 * Include this header in exactly one .cpp of the game project.
 *
 * Example:
 * ```
 *   #include <Catty/Catty.h>
 *   #include <Catty/EntryPoint.h>
 *
 *   class FMyGameApp : public Catty::FApp
 *   {
 *   protected:
 *       virtual void Configure(Catty::FEngineConfig& OutConfig) override
 *       {
 *           OutConfig.ApplicationName = "MyGame";
 *           OutConfig.ProjectConfigDir = "Config";
 *       }
 *       virtual bool PostInitialize() override
 *       {
 *           PushLayer(std::make_unique<FWorldLayer>());
 *           return true;
 *       }
 *   };
 *
 *   Catty::FApp* Catty::CreateApplication()
 *   {
 *       return new FMyGameApp();
 *   }
 * ```
 */

#include "Catty/Core/App.h"
#include "Catty/Core/Log.h"

int main(int Argc, char** Argv)
{
	(void)Argc;
	(void)Argv;

	// Bootstrap console logger before CreateApplication / Run reconfigure with file sinks.
	{
		Catty::FLogConfig BootstrapConfig;
		BootstrapConfig.bEnableConsole = true;
		BootstrapConfig.bEnableFile = false;
		Catty::FLog::Initialize(BootstrapConfig);
	}

	Catty::FApp* App = Catty::CreateApplication();
	if (!App)
	{
		CATTY_CORE_ERROR("CreateApplication returned null");
		Catty::FLog::Shutdown();
		return 1;
	}

	App->Run();
	delete App;

	if (Catty::FLog::IsInitialized())
	{
		Catty::FLog::Shutdown();
	}
	return 0;
}
