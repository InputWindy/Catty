#include "Catty/Core/App.h"
#include "Catty/Core/Log.h"
#include "Catty/Core/Timer.h"
#include "Catty/Platform/PlatformWindow.h"

#include <imgui.h>

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
	if (ImGui.IsInitialized() && RenderServer.IsInitialized())
	{
		ImGui.Shutdown(RenderServer);
	}
	if (RenderServer.IsInitialized())
	{
		RenderServer.Shutdown();
	}
	if (ResourceServer.IsInitialized())
	{
		ResourceServer.Shutdown();
	}
	ShutdownPlatformWindow(PlatformWindow);
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
		Engine.Shutdown();
		FPlatformWindowFactory::Shutdown();
		return false;
	}

	if (PlatformDesc.bHeadless)
	{
		bAutoExitAfterFrames = true;
		CATTY_CORE_INFO("Platform window headless; auto-exit after {} frames", AutoExitFrameCount);
	}

	if (!RenderServer.Initialize())
	{
		CATTY_CORE_ERROR("FApp::InitializeEngine failed (RenderServer)");
		ShutdownPlatformWindow(PlatformWindow);
		Engine.Shutdown();
		return false;
	}

	if (!RenderServer.InitializeRHI(*PlatformWindow))
	{
		CATTY_CORE_ERROR("FApp::InitializeEngine failed (RHI)");
		RenderServer.Shutdown();
		ShutdownPlatformWindow(PlatformWindow);
		Engine.Shutdown();
		return false;
	}

	if (PlatformWindow->HasOsWindow())
	{
		if (!ImGui.Initialize(*PlatformWindow, RenderServer))
		{
			CATTY_CORE_ERROR("FApp::InitializeEngine failed (ImGui)");
			RenderServer.Shutdown();
			ShutdownPlatformWindow(PlatformWindow);
			Engine.Shutdown();
			return false;
		}
	}

	if (!ResourceServer.Initialize())
	{
		CATTY_CORE_ERROR("FApp::InitializeEngine failed (ResourceServer)");
		if (ImGui.IsInitialized())
		{
			ImGui.Shutdown(RenderServer);
		}
		RenderServer.Shutdown();
		ShutdownPlatformWindow(PlatformWindow);
		Engine.Shutdown();
		return false;
	}

	if (PlatformWindow->HasOsWindow())
	{
		PlatformWindow->GetFramebufferSize(LastFramebufferWidth, LastFramebufferHeight);
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
	FlushRenderServer();
	if (ImGui.IsInitialized())
	{
		ImGui.Shutdown(RenderServer);
	}
	if (RenderServer.IsInitialized())
	{
		RenderServer.Shutdown();
	}
	if (ResourceServer.IsInitialized())
	{
		ResourceServer.Shutdown();
	}
	ShutdownPlatformWindow(PlatformWindow);
	if (Engine.IsInitialized())
	{
		Engine.Shutdown();
	}
}

void FApp::BeginFrame(float /*DeltaSeconds*/)
{
	ImGui.BeginFrame();
}

void FApp::ProcessInput(float /*DeltaSeconds*/)
{
	if (!PlatformWindow)
	{
		return;
	}

	Input.Update(*PlatformWindow);

	const bool bImGuiWantsKeyboard = ImGui.IsInitialized() && ImGui::GetIO().WantCaptureKeyboard;
	if (!bImGuiWantsKeyboard && Input.WasKeyPressed(EKey::Escape))
	{
		RequestExit();
	}
}

void FApp::FixedUpdate(float /*InFixedDeltaSeconds*/)
{
}

void FApp::Update(float /*DeltaSeconds*/)
{
	if (ImGui.IsInitialized() && ImGui.bShowDemoWindow)
	{
		ImGui::ShowDemoWindow(&ImGui.bShowDemoWindow);
	}
}

void FApp::LateUpdate(float /*DeltaSeconds*/)
{
}

void FApp::PreRender(float /*DeltaSeconds*/)
{
}

void FApp::Render(float /*DeltaSeconds*/)
{
	ImGui.EndFrame();

	if (!RenderServer.HasRHI())
	{
		return;
	}

	const FEngineConfig& Config = Engine.GetConfig();
	RenderServer.RequestClearPresent(
		Config.ClearColorR,
		Config.ClearColorG,
		Config.ClearColorB,
		Config.ClearColorA);
}

void FApp::EndFrame(float /*DeltaSeconds*/)
{
}

float FApp::CalculateDeltaSeconds()
{
	const double NowSeconds = PlatformWindow ? PlatformWindow->GetTimeSeconds() : 0.0;
	float DeltaSeconds = static_cast<float>(NowSeconds - LastFrameTimeSeconds);
	LastFrameTimeSeconds = NowSeconds;

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

void FApp::SyncFramebufferSize()
{
	if (!PlatformWindow || !PlatformWindow->HasOsWindow() || !RenderServer.HasRHI())
	{
		return;
	}

	int Width = 0;
	int Height = 0;
	PlatformWindow->GetFramebufferSize(Width, Height);
	if (Width <= 0 || Height <= 0)
	{
		return;
	}

	if (Width != LastFramebufferWidth || Height != LastFramebufferHeight)
	{
		LastFramebufferWidth = Width;
		LastFramebufferHeight = Height;
		RenderServer.RequestResize(Width, Height);
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
		SyncFramebufferSize();
	}

	BeginFrame(DeltaSeconds);
	ProcessInput(DeltaSeconds);
	if (!bRunning)
	{
		return;
	}

	RunFixedUpdates(DeltaSeconds);
	Engine.Tick(DeltaSeconds);
	Update(DeltaSeconds);
	LateUpdate(DeltaSeconds);

	PreRender(DeltaSeconds);
	Render(DeltaSeconds);
	// Consume ImGui draw data on the render thread before the next NewFrame.
	FlushRenderServer();
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

	FlushRenderServer();

	PreShutdown();
	Shutdown();
	Timer.ClearActive();
	ShutdownAppLogging(*this);
}

} // namespace Catty
