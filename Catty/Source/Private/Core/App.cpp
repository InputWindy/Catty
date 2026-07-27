#include "Catty/Core/App.h"
#include "Catty/Core/ConsoleManager.h"
#include "Catty/Core/Log.h"
#include "Catty/Core/Timer.h"
#include "Catty/Platform/PlatformWindow.h"

#include <imgui.h>

#include <algorithm>
#include <utility>

namespace Catty
{

namespace
{

static TAutoConsoleVariable GCVarFixedDeltaSeconds(
	"t.FixedDeltaSeconds",
	1.0f / 50.0f,
	"Fixed simulation step in seconds (0 disables fixed updates)");

static TAutoConsoleVariable GCVarMaxFixedUpdatesPerFrame(
	"t.MaxFixedUpdatesPerFrame",
	5,
	"Max fixed updates per frame (spiral-of-death clamp)");

static TAutoConsoleVariable GCVarMaxDeltaSeconds(
	"t.MaxDeltaSeconds",
	0.25f,
	"Clamp frame delta seconds after hitch / debugger pause");

static TAutoConsoleVariable GCVarHeadlessAutoExitFrames(
	"app.Headless.AutoExitFrames",
	3,
	"Headless auto-exit after N frames (when Window.Create=0)");

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

/** Load Config/DefaultEngine.ini [ConsoleVariables] into the CVar registry, then mirror engine CVars into FEngineConfig.
 *  Safe to call before FLog::Initialize (does not log).
 *  @return Number of overrides applied/queued, or -1 if file missing.
 */
int LoadProjectEngineIni(FEngineConfig& Config)
{
	const std::string IniPath = Config.ProjectConfigDir + "/DefaultEngine.ini";
	const int Applied = FConsoleManager::Get().LoadConsoleVariablesFromIni(IniPath);
	ApplyEngineCVarsToConfig(Config);
	return Applied;
}

} // namespace

FApp::FApp() = default;

FApp::~FApp()
{
	LayerStack.Clear();
	if (ImGui.IsInitialized() && RenderServer.IsInitialized())
	{
		ImGui.Shutdown(RenderServer);
	}
	if (RenderServer.IsInitialized())
	{
		RenderServer.Shutdown();
	}
	if (ResourceManager.IsInitialized())
	{
		ResourceManager.Shutdown();
	}
	if (WorkerPool.IsInitialized())
	{
		WorkerPool.Shutdown();
	}
	if (GCManager.IsInitialized())
	{
		GCManager.Shutdown();
	}
	ShutdownPlatformWindow(PlatformWindow);
	if (Engine.IsInitialized())
	{
		Engine.Shutdown();
	}
}

float FApp::GetFixedDeltaSeconds() const
{
	return GCVarFixedDeltaSeconds.GetValue();
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
		AutoExitFrameCount = static_cast<std::uint64_t>(
			(std::max)(1, GCVarHeadlessAutoExitFrames.GetValue()));
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
		if (!ImGui.Initialize(*PlatformWindow, RenderServer, EngineConfig.ProjectConfigDir))
		{
			CATTY_CORE_ERROR("FApp::InitializeEngine failed (ImGui)");
			RenderServer.Shutdown();
			ShutdownPlatformWindow(PlatformWindow);
			Engine.Shutdown();
			return false;
		}
	}

	if (!GCManager.Initialize())
	{
		CATTY_CORE_ERROR("FApp::InitializeEngine failed (GCManager)");
		if (ImGui.IsInitialized())
		{
			ImGui.Shutdown(RenderServer);
		}
		RenderServer.Shutdown();
		ShutdownPlatformWindow(PlatformWindow);
		Engine.Shutdown();
		return false;
	}

	if (!ResourceManager.Initialize(GCManager))
	{
		CATTY_CORE_ERROR("FApp::InitializeEngine failed (ResourceManager)");
		GCManager.Shutdown();
		if (ImGui.IsInitialized())
		{
			ImGui.Shutdown(RenderServer);
		}
		RenderServer.Shutdown();
		ShutdownPlatformWindow(PlatformWindow);
		Engine.Shutdown();
		return false;
	}

	if (!WorkerPool.Initialize())
	{
		CATTY_CORE_ERROR("FApp::InitializeEngine failed (WorkerPool)");
		ResourceManager.Shutdown();
		GCManager.Shutdown();
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
	LayerStack.Clear();
	if (ImGui.IsInitialized())
	{
		ImGui.Shutdown(RenderServer);
	}
	if (RenderServer.IsInitialized())
	{
		RenderServer.Shutdown();
	}
	if (ResourceManager.IsInitialized())
	{
		ResourceManager.Shutdown();
	}
	if (WorkerPool.IsInitialized())
	{
		WorkerPool.Shutdown();
	}
	if (GCManager.IsInitialized())
	{
		GCManager.Shutdown();
	}
	ShutdownPlatformWindow(PlatformWindow);
	if (Engine.IsInitialized())
	{
		Engine.Shutdown();
	}
}

void FApp::PushLayer(std::unique_ptr<FLayer> Layer)
{
	LayerStack.PushLayer(std::move(Layer));
}

void FApp::PushOverlay(std::unique_ptr<FLayer> Overlay)
{
	LayerStack.PushOverlay(std::move(Overlay));
}

void FApp::BeginFrame(float DeltaSeconds)
{
	ImGui.BeginFrame();
	LayerStack.BeginFrame(DeltaSeconds);
}

void FApp::ProcessInput(float DeltaSeconds)
{
	if (ImGui.IsInitialized())
	{
		const ImGuiIO& IO = ImGui::GetIO();
		if (!IO.WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			RequestExit();
		}
	}

	LayerStack.ProcessInput(DeltaSeconds);
}

void FApp::FixedUpdate(float InFixedDeltaSeconds)
{
	LayerStack.FixedUpdate(InFixedDeltaSeconds);
}

void FApp::Update(float DeltaSeconds)
{
	GCManager.Tick(DeltaSeconds);
	LayerStack.Update(DeltaSeconds);
}

void FApp::LateUpdate(float DeltaSeconds)
{
	LayerStack.LateUpdate(DeltaSeconds);
}

void FApp::PreRender(float DeltaSeconds)
{
	LayerStack.PreRender(DeltaSeconds);
}

void FApp::Render(float DeltaSeconds)
{
	LayerStack.Render(DeltaSeconds);
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

void FApp::EndFrame(float DeltaSeconds)
{
	LayerStack.EndFrame(DeltaSeconds);
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

	const float MaxDeltaSeconds = (std::max)(0.0f, GCVarMaxDeltaSeconds.GetValue());
	if (MaxDeltaSeconds <= 0.0f)
	{
		return DeltaSeconds;
	}
	return (std::min)(DeltaSeconds, MaxDeltaSeconds);
}

void FApp::RunFixedUpdates(float DeltaSeconds)
{
	const float FixedDeltaSeconds = GCVarFixedDeltaSeconds.GetValue();
	const int MaxFixedUpdatesPerFrame = GCVarMaxFixedUpdatesPerFrame.GetValue();
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
	// Apply ini before logging so log.* / app.Name CVars take effect on first init.
	const int IniApplied = LoadProjectEngineIni(EngineConfig);
	InitializeAppLogging(EngineConfig);
	if (IniApplied < 0)
	{
		CATTY_CORE_WARN(
			"DefaultEngine.ini not found (looked for '{}/DefaultEngine.ini') — using CVar defaults",
			EngineConfig.ProjectConfigDir);
	}
	else
	{
		CATTY_CORE_INFO(
			"Loaded/queued {} CVar override(s) from '{}/DefaultEngine.ini'",
			IniApplied,
			EngineConfig.ProjectConfigDir);
	}
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
