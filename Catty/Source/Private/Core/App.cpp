#include "Catty/Core/App.h"

#include <iostream>

namespace Catty
{

FApp::FApp() = default;

FApp::~FApp()
{
	if (Engine.IsInitialized())
	{
		Engine.Shutdown();
	}
}

void FApp::Configure(FEngineConfig& /*OutConfig*/)
{
}

bool FApp::Initialize()
{
	Configure(EngineConfig);
	if (!Engine.Initialize(EngineConfig))
	{
		std::cerr << "[Catty] FApp::Initialize failed\n";
		return false;
	}
	return true;
}

void FApp::Tick(float DeltaSeconds)
{
	Engine.Tick(DeltaSeconds);
}

void FApp::Shutdown()
{
	Engine.Shutdown();
}

void FApp::RequestExit()
{
	bRunning = false;
}

void FApp::Run()
{
	if (!Initialize())
	{
		return;
	}

	bRunning = true;
	while (bRunning)
	{
		constexpr float DeltaSeconds = 1.0f / 60.0f;
		Tick(DeltaSeconds);
	}

	Shutdown();
}

} // namespace Catty
