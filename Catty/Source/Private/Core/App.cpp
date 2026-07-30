#include <Core/App.h>
#include <Core/ConsoleManager.h>
#include <Core/Log.h>
#include <Core/Paths.h>
#include <Core/Modules/GC.h>
#include <Core/Modules/Platform.h>
#include <Core/Modules/Render.h>
#include "Modules/ResourceManager.h"
#include <Core/Timer.h>
#include <Core/WorkerPool.h>

#include <algorithm>
#include <atomic>
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
	LogConfig.bEnableConsole = false;
	LogConfig.bEnableFile = false;
	LogConfig.bEnableEditorCapture = true;
	App.GetLog().Initialize(LogConfig);
}

void ApplyAppLoggingFromConfig(FApp& App, const FEngineConfig& Config)
{
	FLogConfig LogConfig;
	LogConfig.CoreLoggerName = "Catty";
	LogConfig.ClientLoggerName = Config.ApplicationName.empty() ? "App" : Config.ApplicationName;
	LogConfig.LogDirectory = Config.SavedDir + "/Logs";
	LogConfig.bEnableConsole = false;
	LogConfig.bEnableFile = true;
	LogConfig.bEnableEditorCapture = true;
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
	RegisterModule(std::make_unique<FPlatform>());
	RegisterModule(std::make_unique<FRender>());
	RegisterModule(std::make_unique<FGC>());
	RegisterModule(std::make_unique<FResourceManager>());
}

bool FApp::PostInitialize()
{
	return true;
}

void FApp::OnRequestExit()
{
	if (GetState() != EAppState::Running)
	{
		return;
	}

	CATTY_CORE_INFO("FApp: exit requested — entering WaitForExit (drain pipeline)");
	RequestWaitForExit();
}

void FApp::OnDetachModule(IModule& Module)
{
	Module.GetOnExitRequested().RemoveAll(this);
	Module.GetOnDetach().RemoveAll(this);
}

bool FApp::Initialize()
{
	// Game fills path defaults / ApplicationName first (often relative).
	Configure(EngineConfig);

	// Resolve project / engine roots and absolutize Config / Saved / Content / …
	FPaths::Initialize(EngineConfig);

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

	CATTY_CORE_INFO("FPaths: ProjectDir = {}", EngineConfig.ProjectDir);
	CATTY_CORE_INFO("FPaths: EngineDir  = {}", EngineConfig.EngineDir);
	CATTY_CORE_INFO("FPaths: Config     = {}", EngineConfig.ProjectConfigDir);
	CATTY_CORE_INFO("FPaths: Content    = {}", EngineConfig.ProjectContentDir);
	CATTY_CORE_INFO("FPaths: Saved      = {}", EngineConfig.SavedDir);
	CATTY_CORE_INFO("FPaths: EngShaders = {}", EngineConfig.EngineShadersDir);
	CATTY_CORE_INFO("FPaths: EngContent = {}", FPaths::GetEngineContentDir());
	for (const FPathMount& Mount : FPaths::GetMountPoints())
	{
		CATTY_CORE_INFO("FPaths: Mount {} -> {}", Mount.VirtualRoot, Mount.DiskRoot);
	}

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
	if (!ExecuteLifecycleStage(EModuleStage::PreInit, Ctx)
		|| !ExecuteLifecycleStage(EModuleStage::Init, Ctx)
		|| !ExecuteLifecycleStage(EModuleStage::PostInit, Ctx))
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

	for (const auto& Module : Modules)
	{
		if (Module)
		{
			(void)GetA().Register(*Module);
		}
	}
	for (const std::unique_ptr<FLayer>& Layer : Layers)
	{
		if (Layer)
		{
			(void)GetB().Register(*Layer);
		}
	}

	if (!BuildGraph())
	{
		CATTY_CORE_ERROR("FApp::Initialize: BuildGraph failed");
		Shutdown();
		ShutdownAppLogging(*this);
		return false;
	}

	LastFrameTimeSeconds = std::chrono::duration<double>(
		std::chrono::steady_clock::now().time_since_epoch()).count();

	return true;
}

void FApp::Shutdown()
{
	FStageContext Ctx{};
	Ctx.FrameIndex = FrameIndex;

	ExecuteLifecycleStage(EModuleStage::Shutdown, Ctx);

	ClearLayers();
	DetachModules();

	if (WorkerPool.IsInitialized())
	{
		WorkerPool.Shutdown();
	}
}

void FApp::Run()
{
	if (!Initialize())
	{
		return;
	}

	if (!Execute())
	{
		CATTY_CORE_ERROR("FApp::Execute failed or was not built");
	}
	Shutdown();
	ShutdownAppLogging(*this);
}

void FApp::OnWorkersStarted()
{
	BootstrapFirstAttach();
}

void FApp::MakeStageContext(FStageContext& Out) const
{
	Out.DeltaSeconds = DeltaSeconds;
	Out.FixedDeltaSeconds = FixedDeltaSeconds;
	Out.FrameIndex = FrameIndex;
}

bool FApp::BuildGraph()
{
	using FModuleStage = FModuleFrameTraits::FStage;
	using FLayerStage = FLayerFrameTraits::FStage;

	TSequencerDep<FModuleFrameTraits, FLayerFrameTraits> ModuleToLayer;
	TSequencerDep<FLayerFrameTraits, FModuleFrameTraits> LayerToModule;

	// Module pin → Layer gate (same logical stage on both sides when enums align).
	const auto WireSame = [&](FModuleStage ModuleStage, FLayerStage LayerStage)
	{
		ModuleToLayer.FromTo[ModuleStage] = LayerStage;
	};
	// Layer pin → Module gate (advance Module to the next stage).
	const auto WireNext = [&](FLayerStage FromLayer, FModuleStage ToModule)
	{
		LayerToModule.FromTo[FromLayer] = ToModule;
	};

	WireSame(FModuleStage::Attach, FLayerStage::Attach);
	WireNext(FLayerStage::Attach, FModuleStage::BeginFrame);
	WireSame(FModuleStage::BeginFrame, FLayerStage::BeginFrame);
	WireNext(FLayerStage::BeginFrame, FModuleStage::ProcessInput);
	WireSame(FModuleStage::ProcessInput, FLayerStage::ProcessInput);
	// Layer.ProcessInput → Module.FixedUpdate|Update via InstallFixedUpdateRepeat
	WireSame(FModuleStage::FixedUpdate, FLayerStage::FixedUpdate);
	// Layer.FixedUpdate → Module.Update / reopen via InstallFixedUpdateRepeat
	WireSame(FModuleStage::Update, FLayerStage::Update);
	WireNext(FLayerStage::Update, FModuleStage::LateUpdate);
	WireSame(FModuleStage::LateUpdate, FLayerStage::LateUpdate);
	WireNext(FLayerStage::LateUpdate, FModuleStage::PreRender);
	WireSame(FModuleStage::PreRender, FLayerStage::PreRender);
	WireNext(FLayerStage::PreRender, FModuleStage::Render);
	WireSame(FModuleStage::Render, FLayerStage::Render);
	WireNext(FLayerStage::Render, FModuleStage::PostRender);
	WireSame(FModuleStage::PostRender, FLayerStage::PostRender);
	WireNext(FLayerStage::PostRender, FModuleStage::EndFrame);
	WireSame(FModuleStage::EndFrame, FLayerStage::EndFrame);
	WireNext(FLayerStage::EndFrame, FModuleStage::Detach);
	WireSame(FModuleStage::Detach, FLayerStage::Detach);
	WireNext(FLayerStage::Detach, FModuleStage::Attach);
	WireSame(FModuleStage::PrepareExit, FLayerStage::PrepareExit);
	// PrepareExit is an exit-drain slot; do not also feed Module.Detach (EndFrame already does).

	if (!BindDep(GetA(), GetB(), ModuleToLayer))
	{
		return false;
	}
	if (!BindDep(GetB(), GetA(), LayerToModule))
	{
		return false;
	}
	if (!Build())
	{
		return false;
	}
	InstallFixedUpdateRepeat();
	return true;
}

void FApp::InstallFixedUpdateRepeat()
{
	using FModuleStage = FModuleFrameTraits::FStage;
	using FLayerStage = FLayerFrameTraits::FStage;

	const std::size_t PiIndex = FLayerFrameTraits::StageToIndex(FLayerStage::ProcessInput);
	const std::size_t LayerFuIndex = FLayerFrameTraits::StageToIndex(FLayerStage::FixedUpdate);
	const std::size_t DetachIndex = FLayerFrameTraits::StageToIndex(FLayerStage::Detach);
	const std::size_t ModuleFuIndex = FModuleFrameTraits::StageToIndex(FModuleStage::FixedUpdate);
	const std::size_t UpdateIndex = FModuleFrameTraits::StageToIndex(FModuleStage::Update);

	GetA().BindGateExternalExpect(ModuleFuIndex, 1);
	GetA().BindGateExternalExpect(UpdateIndex, 1);
	GetB().AddPinRaiseListener([this, PiIndex, LayerFuIndex, DetachIndex, ModuleFuIndex, UpdateIndex](std::size_t Raised)
	{
		if (Raised == DetachIndex)
		{
			bFrameTickArmed.store(true, std::memory_order_release);
			return;
		}
		if (Raised == PiIndex)
		{
			if (FixedStepsRemaining <= 0)
			{
				GetA().ForceOpenGate(FModuleStage::Update);
			}
			else
			{
				GetA().NotifyExternalPin(ModuleFuIndex);
			}
			return;
		}
		if (Raised != LayerFuIndex)
		{
			return;
		}
		if (FixedStepsRemaining > 1)
		{
			--FixedStepsRemaining;
			GetA().ForceOpenGate(FModuleStage::FixedUpdate);
		}
		else
		{
			FixedStepsRemaining = 0;
			GetA().NotifyExternalPin(UpdateIndex);
		}
	});
}

void FApp::BootstrapFirstAttach()
{
	using FModuleStage = FModuleFrameTraits::FStage;
	GetA().ForceOpenGate(FModuleStage::Attach);
}

// ---------------------------------------------------------------------------
// Modules
// ---------------------------------------------------------------------------

void FApp::AttachModules()
{
	for (IModule* Module : GetOrderForStage(EModuleStage::Init))
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
	for (IModule* Module : GetOrderForStage(EModuleStage::Shutdown))
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

bool FApp::BuildStageOrder(EModuleStage Stage, std::vector<IModule*>& OutOrder)
{
	OutOrder.clear();

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
		Module->GetDependencies(Stage, Deps);
		for (const std::string& Dep : Deps)
		{
			if (ModulesByName.find(Dep) == ModulesByName.end())
			{
				CATTY_CORE_ERROR(
					"Module '{}' stage {} depends on missing '{}'",
					Name,
					static_cast<int>(Stage),
					Dep);
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
		OutOrder.push_back(ModulesByName[Name]);
		for (const std::string& Next : Adj[Name])
		{
			if (--InDegree[Next] == 0)
			{
				ReadyList.push_back(Next);
			}
		}
	}

	if (OutOrder.size() != Modules.size())
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
			"FATAL: Module dependency cycle at stage {} involving: {}",
			static_cast<int>(Stage),
			Remaining.empty() ? "(unknown)" : Remaining);
		return false;
	}

	return true;
}

bool FApp::RebuildModuleOrder()
{
	ModulesByName.clear();
	for (auto& Order : StageOrders)
	{
		Order.clear();
	}

	for (const auto& Module : Modules)
	{
		ModulesByName[Module->GetName()] = Module.get();
	}

	static constexpr EModuleStage LifecycleStages[] = {
		EModuleStage::PreInit,
		EModuleStage::Init,
		EModuleStage::PostInit,
		EModuleStage::Shutdown,
	};
	for (const EModuleStage Stage : LifecycleStages)
	{
		if (!BuildStageOrder(Stage, StageOrders[StageIndex(Stage)]))
		{
			return false;
		}
	}

	return true;
}

const std::vector<IModule*>& FApp::GetOrderForStage(EModuleStage Stage) const
{
	return StageOrders[StageIndex(Stage)];
}

bool FApp::WaitModuleDependencies(IModule& Module, EModuleStage Stage)
{
	std::vector<std::string> Deps;
	Module.GetDependencies(Stage, Deps);
	for (const std::string& DepName : Deps)
	{
		IModule* Dep = GetModuleByName(DepName.c_str());
		if (!Dep)
		{
			CATTY_CORE_ERROR(
				"Module '{}' missing dependency '{}' at stage {}",
				Module.GetName(),
				DepName,
				static_cast<int>(Stage));
			return false;
		}
		Dep->WaitStageComplete();
	}
	return true;
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

	FLayer* Raw = Layer.get();
	Layers.insert(
		Layers.begin() + static_cast<std::ptrdiff_t>(LayerInsertIndex),
		std::move(Layer));
	++LayerInsertIndex;

	if (IsBuilt())
	{
		GetB().RequestAdd(*Raw);
	}
	else
	{
		Raw->Attach();
	}
}

void FApp::PushOverlay(std::unique_ptr<FLayer> Overlay)
{
	if (!Overlay)
	{
		return;
	}

	FLayer* Raw = Overlay.get();
	Layers.push_back(std::move(Overlay));

	if (IsBuilt())
	{
		GetB().RequestAdd(*Raw);
	}
	else
	{
		Raw->Attach();
	}
}

void FApp::ClearLayers()
{
	for (auto It = Layers.rbegin(); It != Layers.rend(); ++It)
	{
		if (*It)
		{
			(*It)->Detach();
		}
	}
	Layers.clear();
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

bool FApp::IsLifecycleStage(EModuleStage Stage)
{
	return IsInitFamily(Stage) || Stage == EModuleStage::Shutdown;
}

bool FApp::ExecuteLifecycleStage(EModuleStage Stage, FStageContext& Ctx)
{
	assert(IsLifecycleStage(Stage));

	std::atomic<bool> bStageFailed{false};

	for (IModule* Module : GetOrderForStage(Stage))
	{
		if (Module)
		{
			Module->ResetStageFence();
		}
	}

	const std::vector<IModule*>& Order = GetOrderForStage(Stage);

	for (IModule* Module : Order)
	{
		if (!Module || Module->PreferMainThread())
		{
			continue;
		}

		WorkerPool.Push([this, Module, Stage, &Ctx, &bStageFailed]()
		{
			if (!WaitModuleDependencies(*Module, Stage))
			{
				bStageFailed = true;
				Module->SignalStageComplete();
				return;
			}

			Module->SetCurrentStage(Stage);
			if (!Module->ExecuteStage(Stage, *this, Ctx))
			{
				bStageFailed = true;
			}
			Module->SignalStageComplete();
		});
	}

	for (IModule* Module : Order)
	{
		if (!Module || !Module->PreferMainThread())
		{
			continue;
		}

		if (!WaitModuleDependencies(*Module, Stage))
		{
			bStageFailed = true;
			Module->SignalStageComplete();
			continue;
		}

		Module->SetCurrentStage(Stage);
		if (!Module->ExecuteStage(Stage, *this, Ctx))
		{
			bStageFailed = true;
			if (IsInitFamily(Stage))
			{
				CATTY_CORE_ERROR(
					"Module '{}' failed at stage {}",
					Module->GetName(),
					static_cast<int>(Stage));
				Module->SignalStageComplete();
				WorkerPool.Flush();
				return false;
			}
		}
		Module->SignalStageComplete();
	}

	WorkerPool.Flush();

	if (bStageFailed && IsInitFamily(Stage))
	{
		return false;
	}

	return true;
}

bool FApp::AreAllModulesIdle() const
{
	for (const auto& Module : Modules)
	{
		if (Module && !Module->IsIdle())
		{
			return false;
		}
	}
	return true;
}

void FApp::UpdateAppState()
{
	if (!bFrameTickArmed.exchange(false, std::memory_order_acq_rel))
	{
		return;
	}

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

	const float FixedDelta = GCVarFixedDeltaSeconds.GetValue();
	const int MaxFixed = GCVarMaxFixedUpdatesPerFrame.GetValue();
	FixedDeltaSeconds = FixedDelta;
	FixedStepsRemaining = 0;
	if (FixedDelta > 0.0f && MaxFixed > 0)
	{
		FixedUpdateAccumulator += DeltaSeconds;
		int Steps = 0;
		while (FixedUpdateAccumulator >= FixedDelta && Steps < MaxFixed)
		{
			FixedUpdateAccumulator -= FixedDelta;
			++Steps;
		}
		if (Steps >= MaxFixed)
		{
			FixedUpdateAccumulator = 0.0f;
		}
		FixedStepsRemaining = Steps;
	}
}

void IModule::OnSequencerStage(EFrameStage Stage)
{
	if (!GApp)
	{
		return;
	}

	if (Stage == EFrameStage::BeginFrame)
	{
		GApp->UpdateAppState();
	}

	if (GApp->GetState() == EAppState::WaitForExit)
	{
		const bool bDrain = Stage == EFrameStage::BeginFrame
			|| Stage == EFrameStage::PrepareExit
			|| Stage == EFrameStage::Update
			|| Stage == EFrameStage::EndFrame
			|| Stage == EFrameStage::Attach
			|| Stage == EFrameStage::Detach;
		if (!bDrain)
		{
			return;
		}
		if (Stage == EFrameStage::PrepareExit || Stage == EFrameStage::EndFrame)
		{
			if (GApp->AreAllModulesIdle() && GApp->GetWorkerPool().IsIdle())
			{
				GApp->RequestStopAll();
			}
		}
	}

	const EModuleStage ModuleStage = FrameStageToModuleStage(Stage);
	if (ModuleStage == EModuleStage::NumMaxStage)
	{
		return;
	}

	FStageContext Ctx{};
	GApp->MakeStageContext(Ctx);
	if (Stage == EFrameStage::FixedUpdate)
	{
		Ctx.DeltaSeconds = Ctx.FixedDeltaSeconds;
	}
	SetCurrentStage(ModuleStage);
	// Frame work runs on this module's Sequencer thread; no FApp::ExecuteLifecycleStage.
	(void)ExecuteStage(ModuleStage, *GApp, Ctx);
}

} // namespace Catty
