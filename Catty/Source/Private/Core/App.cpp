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

bool FApp::PreInitialize()
{
	return true;
}

void FApp::Configure(FEngineConfig& /*OutConfig*/)
{
}

bool FApp::InitializeEngine()
{
	if (!Engine.Initialize(EngineConfig))
	{
		std::cerr << "[Catty] FApp::InitializeEngine failed\n";
		return false;
	}
	return true;
}

bool FApp::PostInitialize()
{
	return true;
}

void FApp::PreShutdown()
{
}

void FApp::Shutdown()
{
	if (Engine.IsInitialized())
	{
		Engine.Shutdown();
	}
}

void FApp::BeginFrame(float /*DeltaSeconds*/)
{
}

void FApp::ProcessInput(float /*DeltaSeconds*/)
{
}

void FApp::FixedUpdate(float /*InFixedDeltaSeconds*/)
{
}

void FApp::Update(float /*DeltaSeconds*/)
{
}

void FApp::LateUpdate(float /*DeltaSeconds*/)
{
}

void FApp::Render(float /*DeltaSeconds*/)
{
}

void FApp::EndFrame(float /*DeltaSeconds*/)
{
}

float FApp::CalculateDeltaSeconds()
{
	constexpr float FrameDeltaSeconds = 1.0f / 60.0f;
	return FrameDeltaSeconds;
}

void FApp::RunFixedUpdates(float DeltaSeconds)
{
	if (FixedDeltaSeconds <= 0.0f || MaxFixedUpdatesPerFrame <= 0)
	{
		return;
	}

	FixedUpdateAccumulator += DeltaSeconds;

	int Steps = 0;
	while (FixedUpdateAccumulator >= FixedDeltaSeconds && Steps < MaxFixedUpdatesPerFrame)
	{
		FixedUpdate(FixedDeltaSeconds);
		FixedUpdateAccumulator -= FixedDeltaSeconds;
		++Steps;
	}

	// Drop leftover time if we hit the step cap (hitch / spiral-of-death guard).
	if (Steps >= MaxFixedUpdatesPerFrame)
	{
		FixedUpdateAccumulator = 0.0f;
	}
}

void FApp::Tick(float DeltaSeconds)
{
	BeginFrame(DeltaSeconds);
	ProcessInput(DeltaSeconds);
	RunFixedUpdates(DeltaSeconds);
	Engine.Tick(DeltaSeconds);
	Update(DeltaSeconds);
	LateUpdate(DeltaSeconds);
	Render(DeltaSeconds);
	EndFrame(DeltaSeconds);
}

void FApp::RequestExit()
{
	bRunning = false;
}

void FApp::Run()
{
	if (!PreInitialize())
	{
		std::cerr << "[Catty] FApp::PreInitialize failed\n";
		return;
	}

	Configure(EngineConfig);

	if (!InitializeEngine())
	{
		return;
	}

	if (!PostInitialize())
	{
		std::cerr << "[Catty] FApp::PostInitialize failed\n";
		PreShutdown();
		Shutdown();
		return;
	}

	bRunning = true;
	while (bRunning)
	{
		Tick(CalculateDeltaSeconds());
	}

	PreShutdown();
	Shutdown();
}

} // namespace Catty
