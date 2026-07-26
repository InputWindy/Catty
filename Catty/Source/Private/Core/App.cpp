#include "Catty/Core/App.h"
#include "Catty/Core/Log.h"

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

} // namespace

FApp::FApp() = default;

FApp::~FApp()
{
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

void FApp::FlushRenderServer()
{
	if (RenderServer.IsInitialized())
	{
		RenderServer.Flush();
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

	// PreRender sync point: game thread waits for render-server task queue to drain.
	FlushRenderServer();
	PreRender(DeltaSeconds);

	Render(DeltaSeconds);
	EndFrame(DeltaSeconds);

	// FApp owns exit policy. Games should not wire RequestExit into Update.
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

	if (!InitializeEngine())
	{
		FLog::Shutdown();
		return;
	}

	if (!PostInitialize())
	{
		CATTY_CORE_ERROR("FApp::PostInitialize failed");
		PreShutdown();
		Shutdown();
		FLog::Shutdown();
		return;
	}

	bRunning = true;
	while (bRunning)
	{
		Tick(CalculateDeltaSeconds());
	}

	PreShutdown();
	Shutdown();
	FLog::Shutdown();
}

} // namespace Catty
