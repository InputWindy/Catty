#include <Core/App.h>
#include <Core/ConsoleManager.h>
#include <Core/Log.h>
#include <Core/Modules/GCModule.h>
#include <Core/Modules/PlatformModule.h>
#include <Core/Modules/RenderModule.h>
#include <Core/Modules/ResourceModule.h>
#include <Core/Timer.h>
#include <Core/WorkerPool.h>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <memory>
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

void BootstrapAppLogging(FApp& App)
{
	FLogConfig LogConfig;
	LogConfig.CoreLoggerName = "Catty";
	LogConfig.ClientLoggerName = "App";
	LogConfig.bEnableConsole = true;
	LogConfig.bEnableFile = false;
	App.GetLog().Initialize(LogConfig);
}

void ApplyAppLoggingFromConfig(FApp& App, const FEngineConfig& Config)
{
	FLogConfig LogConfig;
	LogConfig.CoreLoggerName = "Catty";
	LogConfig.ClientLoggerName = Config.ApplicationName.empty() ? "App" : Config.ApplicationName;
	LogConfig.LogDirectory = Config.SavedDir + "/Logs";
	LogConfig.bEnableConsole = true;
	LogConfig.bEnableFile = true;
	App.GetLog().Initialize(LogConfig);
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
	App.GetLog().Shutdown();
}

int LoadProjectEngineIni(FApp& App, FEngineConfig& Config)
{
	const std::string IniPath = Config.ProjectConfigDir + "/DefaultEngine.ini";
	const int Applied = App.GetConsoleManager().LoadConsoleVariablesFromIni(IniPath);
	ApplyEngineCVarsToConfig(Config);
	return Applied;
}

} // namespace

FApp* GApp = nullptr;

// ---------------------------------------------------------------------------
// Type aliases & lifetime
// ---------------------------------------------------------------------------

FApp::FApp()
{
	GApp = this;

	// Earliest services, in order: Log → ConsoleManager → Timer → WorkerPool.
	// ConsoleManager / Timer / WorkerPool are ready via member construction order; Log needs sinks now.
	BootstrapAppLogging(*this);
	CATTY_CORE_INFO("FApp core services ready (Log, ConsoleManager, Timer, WorkerPool)");
}

FApp::~FApp()
{
	ClearLayers();
	DetachModules();
	if (Log.IsInitialized())
	{
		Log.Shutdown();
	}
	if (GApp == this)
	{
		GApp = nullptr;
	}
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void FApp::Configure(FEngineConfig& /*OutConfig*/)
{
}

void FApp::RegisterModules()
{
	RegisterModule(std::make_unique<FPlatformModule>());
	RegisterModule(std::make_unique<FRenderModule>());
	RegisterModule(std::make_unique<FGCModule>());
	RegisterModule(std::make_unique<FResourceModule>());
}

bool FApp::PostInitialize()
{
	return true;
}

void FApp::PreShutdown()
{
}

void FApp::OnRequestExit()
{
	bRunning = false;
}

void FApp::OnAttachModule(IModule& /*Module*/)
{
}

void FApp::OnDetachModule(IModule& Module)
{
	Module.GetOnAttach().RemoveAll(this);
	Module.GetOnDetach().RemoveAll(this);
}

void FApp::OnAttachLayer(FLayer& /*Layer*/)
{
	RebuildLayerPostBindings();
}

void FApp::OnDetachLayer(FLayer& Layer)
{
	RemoveLayerPostBindings(Layer);
}

bool FApp::Initialize()
{
	// Game fills path defaults / ApplicationName first.
	Configure(EngineConfig);

	// Load DefaultEngine.ini CVars, then sync into EngineConfig.
	const int IniApplied = LoadProjectEngineIni(*this, EngineConfig);
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

	// Logging sinks need final SavedDir / ApplicationName from Configure + ini.
	ApplyAppLoggingFromConfig(*this, EngineConfig);

	RegisterModules();

	if (!RebuildModuleOrder())
	{
		ShutdownAppLogging(*this);
		return false;
	}

	if (!WorkerPool.Initialize())
	{
		CATTY_CORE_ERROR("FApp::Initialize: WorkerPool Initialize failed");
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
		ShutdownAppLogging(*this);
		return false;
	}

	if (!PostInitialize())
	{
		CATTY_CORE_ERROR("FApp::PostInitialize failed");
		Shutdown();
		ShutdownAppLogging(*this);
		return false;
	}

	// Game PostInitialize pushes content layers (World / Editor / Script / …).
	AttachModules();

	bRunning = true;
	LastFrameTimeSeconds = std::chrono::duration<double>(
		std::chrono::steady_clock::now().time_since_epoch()).count();

	return true;
}

void FApp::Shutdown()
{
	FStageContext Ctx{};
	Ctx.FrameIndex = FrameIndex;

	// Stages while modules are still attached (and layers may still exist).
	ExecuteStage(EModuleStage::PreShutdown, Ctx);
	ExecuteStage(EModuleStage::Shutdown, Ctx);

	// Tear down content, then leave module active lifetime.
	ClearLayers();
	DetachModules();

	if (WorkerPool.IsInitialized())
	{
		WorkerPool.Shutdown();
	}

	bRunning = false;
}

void FApp::RunMainLoop()
{
	while (bRunning)
	{
		CATTY_SCOPED_TIMER("Engine", "FApp::Tick");

		UpdateAppState();

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
	PreShutdown();
	Shutdown();
	ShutdownAppLogging(*this);
}

// ---------------------------------------------------------------------------
// Modules
// ---------------------------------------------------------------------------

void FApp::AttachModules()
{
	for (IModule* Module : StartupOrder)
	{
		if (!Module)
		{
			continue;
		}
		Module->Attach();
	}
}

void FApp::DetachModules()
{
	for (IModule* Module : ShutdownOrder)
	{
		if (!Module)
		{
			continue;
		}
		Module->GetOnExitRequested().RemoveAll(this);
		Module->Detach();
	}
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
	(void)Raw->GetOnExitRequested().AddRaw(this, &FApp::OnRequestExit);
	(void)Raw->GetOnAttach().AddRaw(this, &FApp::OnAttachModule);
	(void)Raw->GetOnDetach().AddRaw(this, &FApp::OnDetachModule);
	Modules.push_back(std::move(Module));
}

IModule* FApp::GetModuleByName(const char* Name)
{
	if (!Name)
	{
		return nullptr;
	}
	const auto It = ModulesByName.find(Name);
	return It != ModulesByName.end() ? It->second : nullptr;
}

const IModule* FApp::GetModuleByName(const char* Name) const
{
	return const_cast<FApp*>(this)->GetModuleByName(Name);
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
		std::string Remaining;
		for (const auto& Module : Modules)
		{
			const std::string Name = Module->GetName();
			if (InDegree[Name] > 0)
			{
				if (!Remaining.empty())
				{
					Remaining += ", ";
				}
				Remaining += Name;
			}
		}
		CATTY_CORE_ERROR(
			"FATAL: Module dependency cycle involving: {}",
			Remaining.empty() ? "(unknown)" : Remaining);
		return false;
	}

	ShutdownOrder.assign(StartupOrder.rbegin(), StartupOrder.rend());
	return true;
}

const std::vector<IModule*>& FApp::GetOrderForStage(EModuleStage Stage) const
{
	return IsShutdownFamily(Stage) ? ShutdownOrder : StartupOrder;
}

// ---------------------------------------------------------------------------
// Layers
// ---------------------------------------------------------------------------

void FApp::PushLayer(std::unique_ptr<FLayer> Layer)
{
	if (!Layer)
	{
		return;
	}

	FLayerBinding Entry;
	Entry.Layer = std::move(Layer);
	FLayer* Raw = Entry.Layer.get();
	(void)Raw->GetOnAttach().AddRaw(this, &FApp::OnAttachLayer);
	(void)Raw->GetOnDetach().AddRaw(this, &FApp::OnDetachLayer);
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
	FLayer* Raw = Entry.Layer.get();
	(void)Raw->GetOnAttach().AddRaw(this, &FApp::OnAttachLayer);
	(void)Raw->GetOnDetach().AddRaw(this, &FApp::OnDetachLayer);
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

// ---------------------------------------------------------------------------
// Stage pipeline
// ---------------------------------------------------------------------------

std::size_t FApp::StageIndex(EModuleStage Stage)
{
	const std::size_t Index = static_cast<std::size_t>(Stage);
	assert(Index < static_cast<std::size_t>(EModuleStage::NumMaxStage));
	return Index;
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
		|| Stage == EModuleStage::Shutdown;
}

FApp::FStageMulticast& FApp::GetPostStageDelegate(EModuleStage Stage)
{
	return PostStageDelegates[StageIndex(Stage)];
}

bool FApp::ExecuteStage(EModuleStage Stage, FStageContext& Ctx)
{
	for (IModule* Module : GetOrderForStage(Stage))
	{
		if (!Module->ExecuteStage(Stage, *this, Ctx))
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
	return true;
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

// ---------------------------------------------------------------------------
// Frame state
// ---------------------------------------------------------------------------

void FApp::UpdateAppState()
{
	const double NowSeconds = std::chrono::duration<double>(
		std::chrono::steady_clock::now().time_since_epoch()).count();

	float NewDelta = static_cast<float>(NowSeconds - LastFrameTimeSeconds);
	LastFrameTimeSeconds = NowSeconds;

	if (NewDelta < 0.0f)
	{
		NewDelta = 0.0f;
	}

	const float MaxDeltaSeconds = (std::max)(0.0f, GCVarMaxDeltaSeconds.GetValue());
	if (MaxDeltaSeconds > 0.0f)
	{
		NewDelta = (std::min)(NewDelta, MaxDeltaSeconds);
	}

	DeltaSeconds = NewDelta;
	++FrameIndex;
}

} // namespace Catty
