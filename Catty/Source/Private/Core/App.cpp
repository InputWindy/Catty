#include "Catty/Core/App.h"
#include "Catty/Core/ConsoleManager.h"
#include "Catty/Core/Log.h"
#include "Catty/Core/Modules/PlatformModule.h"
#include "Catty/Core/Timer.h"

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

int LoadProjectEngineIni(FEngineConfig& Config)
{
	const std::string IniPath = Config.ProjectConfigDir + "/DefaultEngine.ini";
	const int Applied = FConsoleManager::Get().LoadConsoleVariablesFromIni(IniPath);
	ApplyEngineCVarsToConfig(Config);
	return Applied;
}

} // namespace

std::size_t FApp::StageIndex(EModuleStage Stage)
{
	return static_cast<std::size_t>(Stage);
}

bool FApp::IsInitFamily(EModuleStage Stage)
{
	return Stage == EModuleStage::PreInit
		|| Stage == EModuleStage::Init
		|| Stage == EModuleStage::PostInit;
}

bool FApp::IsShutdownFamily(EModuleStage Stage)
{
	return Stage == EModuleStage::PreShutdown
		|| Stage == EModuleStage::Shutdown
		|| Stage == EModuleStage::PostShutdown;
}

FApp::FApp() = default;

FApp::~FApp()
{
	ClearLayers();
	DetachModules();
}

void FApp::Configure(FEngineConfig& /*OutConfig*/)
{
}

void FApp::RegisterModules()
{
}

bool FApp::PostInitialize()
{
	return true;
}

void FApp::PreShutdown()
{
}

void FApp::RequestExit()
{
	bRunning = false;
}

FApp::FStageMulticast& FApp::GetPostStageDelegate(EModuleStage Stage)
{
	return PostStageDelegates[StageIndex(Stage)];
}

void FApp::HandleModuleAttach(IModule& Module)
{
	(void)Module.GetOnExitRequested().AddRaw(this, &FApp::RequestExit);
}

void FApp::HandleModuleDetach(IModule& Module)
{
	Module.GetOnExitRequested().RemoveAll(this);
}

void FApp::AttachModules()
{
	for (IModule* Module : StartupOrder)
	{
		if (!Module)
		{
			continue;
		}
		(void)Module->GetOnAttach().AddRaw(this, &FApp::HandleModuleAttach);
		(void)Module->GetOnDetach().AddRaw(this, &FApp::HandleModuleDetach);
		Module->Attach();
	}
}

void FApp::DetachModules()
{
	for (IModule* Module : ShutdownOrder)
	{
		if (Module)
		{
			Module->Detach();
		}
	}
}

void FApp::RemoveLayerPostBindings(FLayer& Layer)
{
	for (FStageMulticast& Delegate : PostStageDelegates)
	{
		Delegate.RemoveAll(&Layer);
	}
}

void FApp::BindLayerPostBindings(FLayer& Layer)
{
	(void)GetPostStageDelegate(EModuleStage::BeginFrame).AddRaw(&Layer, &FLayer::OnBeginFrame);
	(void)GetPostStageDelegate(EModuleStage::FixedUpdate).AddRaw(&Layer, &FLayer::OnFixedUpdate);
	(void)GetPostStageDelegate(EModuleStage::Update).AddRaw(&Layer, &FLayer::OnUpdate);
	(void)GetPostStageDelegate(EModuleStage::LateUpdate).AddRaw(&Layer, &FLayer::OnLateUpdate);
	(void)GetPostStageDelegate(EModuleStage::PreRender).AddRaw(&Layer, &FLayer::OnPreRender);
	(void)GetPostStageDelegate(EModuleStage::Render).AddRaw(&Layer, &FLayer::OnRender);
	(void)GetPostStageDelegate(EModuleStage::PostRender).AddRaw(&Layer, &FLayer::OnPostRender);
	(void)GetPostStageDelegate(EModuleStage::EndFrame).AddRaw(&Layer, &FLayer::OnEndFrame);
}

void FApp::HandleLayerAttach(FLayer& /*Layer*/)
{
	RebuildLayerPostBindings();
}

void FApp::HandleLayerDetach(FLayer& Layer)
{
	RemoveLayerPostBindings(Layer);
}

void FApp::RegisterModule(std::unique_ptr<IModule> Module)
{
	if (!Module)
	{
		return;
	}

	IModule* Raw = Module.get();
	const char* Name = Raw->GetName();
	ModulesByName[Name ? Name : ""] = Raw;
	ModulesByType[std::type_index(typeid(*Raw))] = Raw;
	Modules.push_back(std::move(Module));
	bModuleOrderBuilt = false;
}

bool FApp::RebuildModuleOrder()
{
	StartupOrder.clear();
	ShutdownOrder.clear();
	ModulesByName.clear();

	for (const auto& Module : Modules)
	{
		ModulesByName[Module->GetName()] = Module.get();
	}

	std::unordered_map<std::string, int> InDegree;
	std::unordered_map<std::string, std::vector<std::string>> Adj;

	for (const auto& Module : Modules)
	{
		const std::string Name = Module->GetName();
		InDegree.emplace(Name, 0);
		Adj.emplace(Name, std::vector<std::string>{});
	}

	for (const auto& Module : Modules)
	{
		const std::string Name = Module->GetName();
		std::vector<std::string> Deps;
		Module->GetDependencies(Deps);
		for (const std::string& Dep : Deps)
		{
			if (ModulesByName.find(Dep) == ModulesByName.end())
			{
				CATTY_CORE_ERROR("Module '{}' depends on missing '{}'", Name, Dep);
				return false;
			}
			Adj[Dep].push_back(Name);
			++InDegree[Name];
		}
	}

	std::vector<std::string> ReadyList;
	for (const auto& Module : Modules)
	{
		const std::string Name = Module->GetName();
		if (InDegree[Name] == 0)
		{
			ReadyList.push_back(Name);
		}
	}

	while (!ReadyList.empty())
	{
		const std::string Name = ReadyList.front();
		ReadyList.erase(ReadyList.begin());
		StartupOrder.push_back(ModulesByName[Name]);
		for (const std::string& Next : Adj[Name])
		{
			if (--InDegree[Next] == 0)
			{
				ReadyList.push_back(Next);
			}
		}
	}

	if (StartupOrder.size() != Modules.size())
	{
		CATTY_CORE_ERROR("Module dependency cycle detected");
		return false;
	}

	ShutdownOrder.assign(StartupOrder.rbegin(), StartupOrder.rend());
	bModuleOrderBuilt = true;
	return true;
}

const std::vector<IModule*>& FApp::GetOrderForStage(EModuleStage Stage) const
{
	return IsShutdownFamily(Stage) ? ShutdownOrder : StartupOrder;
}

bool FApp::ExecuteStage(EModuleStage Stage, FStageContext& Ctx)
{
	for (IModule* Module : GetOrderForStage(Stage))
	{
		if (!Module->OnStage(Stage, *this, Ctx))
		{
			if (IsInitFamily(Stage))
			{
				CATTY_CORE_ERROR(
					"Module '{}' failed at stage {}",
					Module->GetName(),
					static_cast<int>(Stage));
				return false;
			}
		}
	}

	PostStageDelegates[StageIndex(Stage)].Broadcast(Stage, *this, Ctx);

	for (IModule* Module : GetOrderForStage(Stage))
	{
		Module->OnPostStage(Stage, *this, Ctx);
	}
	return true;
}

void FApp::RebuildLayerPostBindings()
{
	for (FLayerBinding& Entry : LayerBindings)
	{
		if (Entry.Layer)
		{
			RemoveLayerPostBindings(*Entry.Layer);
		}
	}

	for (FLayerBinding& Entry : LayerBindings)
	{
		if (Entry.Layer)
		{
			BindLayerPostBindings(*Entry.Layer);
		}
	}

	// ProcessInput: overlays first (reverse of stack order).
	GetPostStageDelegate(EModuleStage::ProcessInput).Clear();
	for (auto It = LayerBindings.rbegin(); It != LayerBindings.rend(); ++It)
	{
		if (It->Layer)
		{
			(void)GetPostStageDelegate(EModuleStage::ProcessInput)
				.AddRaw(It->Layer.get(), &FLayer::OnProcessInput);
		}
	}
}

void FApp::PushLayer(std::unique_ptr<FLayer> Layer)
{
	if (!Layer)
	{
		return;
	}

	FLayerBinding Entry;
	Entry.Layer = std::move(Layer);
	Entry.bOverlay = false;
	FLayer* Raw = Entry.Layer.get();
	(void)Raw->GetOnAttach().AddRaw(this, &FApp::HandleLayerAttach);
	(void)Raw->GetOnDetach().AddRaw(this, &FApp::HandleLayerDetach);
	LayerBindings.insert(
		LayerBindings.begin() + static_cast<std::ptrdiff_t>(LayerInsertIndex),
		std::move(Entry));
	++LayerInsertIndex;
	Raw->Attach();
}

void FApp::PushOverlay(std::unique_ptr<FLayer> Overlay)
{
	if (!Overlay)
	{
		return;
	}

	FLayerBinding Entry;
	Entry.Layer = std::move(Overlay);
	Entry.bOverlay = true;
	FLayer* Raw = Entry.Layer.get();
	(void)Raw->GetOnAttach().AddRaw(this, &FApp::HandleLayerAttach);
	(void)Raw->GetOnDetach().AddRaw(this, &FApp::HandleLayerDetach);
	LayerBindings.push_back(std::move(Entry));
	Raw->Attach();
}

void FApp::ClearLayers()
{
	for (auto It = LayerBindings.rbegin(); It != LayerBindings.rend(); ++It)
	{
		if (It->Layer)
		{
			It->Layer->Detach();
		}
	}
	LayerBindings.clear();
	LayerInsertIndex = 0;
}

bool FApp::Initialize()
{
	Configure(EngineConfig);

	RegisterModules();

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

	if (!RebuildModuleOrder())
	{
		Timer.ClearActive();
		ShutdownAppLogging(*this);
		return false;
	}

	FStageContext Ctx{};
	if (!ExecuteStage(EModuleStage::PreInit, Ctx)
		|| !ExecuteStage(EModuleStage::Init, Ctx)
		|| !ExecuteStage(EModuleStage::PostInit, Ctx))
	{
		CATTY_CORE_ERROR("FApp::Initialize module stages failed");
		Shutdown();
		Timer.ClearActive();
		ShutdownAppLogging(*this);
		return false;
	}

	if (!PostInitialize())
	{
		CATTY_CORE_ERROR("FApp::PostInitialize failed");
		Shutdown();
		Timer.ClearActive();
		ShutdownAppLogging(*this);
		return false;
	}

	// Game PostInitialize pushes content layers (World / Editor / Script / …).
	AttachModules();

	bRunning = true;
	if (FPlatformModule* Platform = GetModule<FPlatformModule>())
	{
		if (FPlatformWindow* Window = Platform->GetWindow())
		{
			LastFrameTimeSeconds = Window->GetTimeSeconds();
		}
	}

	return true;
}

void FApp::Shutdown()
{
	PreShutdown();
	ClearLayers();
	DetachModules();

	FStageContext Ctx{};
	Ctx.FrameIndex = FrameIndex;
	ExecuteStage(EModuleStage::PreShutdown, Ctx);
	ExecuteStage(EModuleStage::Shutdown, Ctx);
	ExecuteStage(EModuleStage::PostShutdown, Ctx);
	bRunning = false;
}

float FApp::CalculateDeltaSeconds()
{
	double NowSeconds = LastFrameTimeSeconds;
	if (FPlatformModule* Platform = GetModule<FPlatformModule>())
	{
		if (FPlatformWindow* Window = Platform->GetWindow())
		{
			NowSeconds = Window->GetTimeSeconds();
		}
	}

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

void FApp::RunMainLoop()
{
	while (bRunning)
	{
		CATTY_SCOPED_TIMER("Engine", "FApp::Tick");

		const float DeltaSeconds = CalculateDeltaSeconds();
		++FrameIndex;

		FStageContext Ctx{};
		Ctx.DeltaSeconds = DeltaSeconds;
		Ctx.FixedDeltaSeconds = GCVarFixedDeltaSeconds.GetValue();
		Ctx.FrameIndex = FrameIndex;

		ExecuteStage(EModuleStage::BeginFrame, Ctx);
		if (!bRunning)
		{
			break;
		}

		ExecuteStage(EModuleStage::ProcessInput, Ctx);
		if (!bRunning)
		{
			break;
		}

		const float FixedDelta = GCVarFixedDeltaSeconds.GetValue();
		const int MaxFixed = GCVarMaxFixedUpdatesPerFrame.GetValue();
		if (FixedDelta > 0.0f && MaxFixed > 0)
		{
			FixedUpdateAccumulator += DeltaSeconds;
			int Steps = 0;
			while (FixedUpdateAccumulator >= FixedDelta && Steps < MaxFixed)
			{
				FStageContext FixedCtx = Ctx;
				FixedCtx.DeltaSeconds = FixedDelta;
				FixedCtx.FixedDeltaSeconds = FixedDelta;
				ExecuteStage(EModuleStage::FixedUpdate, FixedCtx);
				FixedUpdateAccumulator -= FixedDelta;
				++Steps;
			}
			if (Steps >= MaxFixed)
			{
				FixedUpdateAccumulator = 0.0f;
			}
		}

		ExecuteStage(EModuleStage::Update, Ctx);
		ExecuteStage(EModuleStage::LateUpdate, Ctx);
		ExecuteStage(EModuleStage::PreRender, Ctx);
		ExecuteStage(EModuleStage::Render, Ctx);
		ExecuteStage(EModuleStage::PostRender, Ctx);
		ExecuteStage(EModuleStage::EndFrame, Ctx);
	}
}

void FApp::Run()
{
	if (!Initialize())
	{
		return;
	}

	RunMainLoop();
	Shutdown();
	Timer.ClearActive();
	ShutdownAppLogging(*this);
}

} // namespace Catty
