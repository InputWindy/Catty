#include "Catty/Core/App.h"
#include "Catty/Core/Log.h"
#include "Catty/Core/Timer.h"
#include "Catty/Platform/PlatformWindow.h"

#include <algorithm>

namespace Catty
{

namespace
{

void InitializeAppLogging(const FEngineConfig& Config)
{
	FLogConfig LogConfig;
	LogConfig.CoreLoggerName = "Catty";
	LogConfig.ClientLoggerName = Config.ApplicationName.empty() ? "App" : Config.ApplicationName;
	LogConfig.LogDirectory = Config.SavedDir + "/Logs";
	LogConfig.bEnableConsole = true;
	LogConfig.bEnableFile = true;
	FLog::Initialize(LogConfig);
}

void ShutdownAppLogging(FApp& App)
{
	for (const FTimerDataPackage& Report : App.GetTimer().QueryAll())
	{
		if (!Report.Samples.empty())
		{
			CATTY_CORE_INFO("{}", Report.Serialize());
		}
	}
	FLog::Shutdown();
}

void ShutdownPlatformWindow(FPlatformWindowPtr& Window)
{
	Window.reset();
	FPlatformWindowFactory::Shutdown();
}

} // namespace

FApp::FApp() = default;

FApp::~FApp()
{
	ShutdownPlatformWindow(PlatformWindow);
	if (ResourceServer.IsInitialized())
	{
		ResourceServer.Shutdown();
	}
	if (RenderServer.IsInitialized())
	{
		RenderServer.Shutdown();
	}
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
		CATTY_CORE_ERROR("FApp::InitializeEngine failed");
		return false;
	}

	if (!RenderServer.Initialize())
	{
		CATTY_CORE_ERROR("FApp::InitializeEngine failed (RenderServer)");
		Engine.Shutdown();
		return false;
	}

	if (!ResourceServer.Initialize())
	{
		CATTY_CORE_ERROR("FApp::InitializeEngine failed (ResourceServer)");
		RenderServer.Shutdown();
		Engine.Shutdown();
		return false;
	}

	FPlatformWindowDesc PlatformDesc;
	PlatformDesc.Platform = EngineConfig.Platform;
	PlatformDesc.Title = EngineConfig.ApplicationName.empty() ? "Catty" : EngineConfig.ApplicationName;
	PlatformDesc.Width = EngineConfig.WindowWidth;
	PlatformDesc.Height = EngineConfig.WindowHeight;
	PlatformDesc.bResizable = EngineConfig.bResizableWindow;
	PlatformDesc.bHeadless = !EngineConfig.bCreateMainWindow;

	PlatformWindow = FPlatformWindowFactory::Create(PlatformDesc);
	if (!PlatformWindow)
	{
		CATTY_CORE_ERROR("FApp::InitializeEngine failed (PlatformWindow)");
		ResourceServer.Shutdown();
		RenderServer.Shutdown();
		Engine.Shutdown();
		FPlatformWindowFactory::Shutdown();
		return false;
	}

	if (PlatformDesc.bHeadless)
	{
		bAutoExitAfterFrames = true;
		CATTY_CORE_INFO("Platform window headless; auto-exit after {} frames", AutoExitFrameCount);
	}

	LastFrameTimeSeconds = PlatformWindow->GetTimeSeconds();
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
	ShutdownPlatformWindow(PlatformWindow);
	if (ResourceServer.IsInitialized())
	{
		ResourceServer.Shutdown();
	}
	if (RenderServer.IsInitialized())
	{
		RenderServer.Shutdown();
	}
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

void FApp::PreRender(float /*DeltaSeconds*/)
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
	const double NowSeconds = PlatformWindow ? PlatformWindow->GetTimeSeconds() : 0.0;
	float DeltaSeconds = static_cast<float>(NowSeconds - LastFrameTimeSeconds);
	LastFrameTimeSeconds = NowSeconds;

	// First frame / clock hiccup guards.
	if (DeltaSeconds < 0.0f)
	{
		DeltaSeconds = 0.0f;
	}

	constexpr float MaxDeltaSeconds = 0.25f;
	return (std::min)(DeltaSeconds, MaxDeltaSeconds);
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

void FApp::FlushRenderServer()
{
	if (RenderServer.IsInitialized())
	{
		RenderServer.Flush();
	}
}

void FApp::Tick(float DeltaSeconds)
{
	CATTY_SCOPED_TIMER("Engine", "FApp::Tick");

	if (PlatformWindow)
	{
		PlatformWindow->PollEvents();
		if (PlatformWindow->ShouldClose())
		{
			RequestExit();
			return;
		}
	}

	BeginFrame(DeltaSeconds);
	ProcessInput(DeltaSeconds);
	RunFixedUpdates(DeltaSeconds);
	Engine.Tick(DeltaSeconds);
	Update(DeltaSeconds);
	LateUpdate(DeltaSeconds);

	// PreRender sync point: game thread waits for render-server task queue to drain.
	FlushRenderServer();
	PreRender(DeltaSeconds);

	Render(DeltaSeconds);
	EndFrame(DeltaSeconds);

	if (bAutoExitAfterFrames && Engine.GetFrameIndex() >= AutoExitFrameCount)
	{
		CATTY_CORE_INFO("Requesting exit after {} frames (headless auto-exit)", AutoExitFrameCount);
		RequestExit();
	}
}

void FApp::RequestExit()
{
	bRunning = false;
}

void FApp::Run()
{
	if (!PreInitialize())
	{
		CATTY_CORE_ERROR("FApp::PreInitialize failed");
		return;
	}

	Configure(EngineConfig);
	InitializeAppLogging(EngineConfig);
	Timer.MakeActive();

	if (!InitializeEngine())
	{
		Timer.ClearActive();
		ShutdownAppLogging(*this);
		return;
	}

	if (!PostInitialize())
	{
		CATTY_CORE_ERROR("FApp::PostInitialize failed");
		PreShutdown();
		Shutdown();
		Timer.ClearActive();
		ShutdownAppLogging(*this);
		return;
	}

	bRunning = true;
	while (bRunning)
	{
		Tick(CalculateDeltaSeconds());
	}

	PreShutdown();
	Shutdown();
	Timer.ClearActive();
	ShutdownAppLogging(*this);
}

} // namespace Catty
