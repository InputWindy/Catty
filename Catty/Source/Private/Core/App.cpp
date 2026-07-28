#include "Catty/Core/App.h"
#include "Catty/Core/ConsoleManager.h"
#include "Catty/Core/Log.h"
#include "Catty/Core/Modules/EngineModule.h"
#include "Catty/Core/Modules/GCModule.h"
#include "Catty/Core/Modules/ImGuiModule.h"
#include "Catty/Core/Modules/PlatformModule.h"
#include "Catty/Core/Modules/RenderModule.h"
#include "Catty/Core/Modules/ResourceModule.h"
#include "Catty/Core/Modules/ScriptModule.h"
#include "Catty/Core/Modules/WorkerModule.h"
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

FApp::FApp()
{
	RegisterDefaultModules();
}

FApp::~FApp()
{
	ClearLayers();
}

void FApp::RegisterDefaultModules()
{
	RegisterModule(std::make_unique<FEngineModule>());
	RegisterModule(std::make_unique<FPlatformModule>());
	RegisterModule(std::make_unique<FRenderModule>());
	RegisterModule(std::make_unique<FImGuiModule>());
	RegisterModule(std::make_unique<FGCModule>());
	RegisterModule(std::make_unique<FResourceModule>());
	RegisterModule(std::make_unique<FWorkerModule>());
	RegisterModule(std::make_unique<FScriptModule>());
}

float FApp::GetFixedDeltaSeconds() const
{
	return GCVarFixedDeltaSeconds.GetValue();
}

void FApp::Configure(FEngineConfig& /*OutConfig*/)
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
	return true;
}

void FApp::UnbindLayerFromPostStages(FLayerBinding& Entry)
{
	GetPostStageDelegate(EModuleStage::BeginFrame).Remove(Entry.BeginFrame);
	GetPostStageDelegate(EModuleStage::ProcessInput).Remove(Entry.ProcessInput);
	GetPostStageDelegate(EModuleStage::FixedUpdate).Remove(Entry.FixedUpdate);
	GetPostStageDelegate(EModuleStage::Update).Remove(Entry.Update);
	GetPostStageDelegate(EModuleStage::LateUpdate).Remove(Entry.LateUpdate);
	GetPostStageDelegate(EModuleStage::PreRender).Remove(Entry.PreRender);
	GetPostStageDelegate(EModuleStage::Render).Remove(Entry.Render);
	GetPostStageDelegate(EModuleStage::PostRender).Remove(Entry.PostRender);
	GetPostStageDelegate(EModuleStage::EndFrame).Remove(Entry.EndFrame);

	Entry.BeginFrame = {};
	Entry.ProcessInput = {};
	Entry.FixedUpdate = {};
	Entry.Update = {};
	Entry.LateUpdate = {};
	Entry.PreRender = {};
	Entry.Render = {};
	Entry.PostRender = {};
	Entry.EndFrame = {};
}

void FApp::RebuildLayerPostBindings()
{
	for (FLayerBinding& Entry : LayerBindings)
	{
		UnbindLayerFromPostStages(Entry);
	}

	auto BindForward = [this](FLayerBinding& Entry)
	{
		FLayer* L = Entry.Layer.get();
		Entry.BeginFrame =
			GetPostStageDelegate(EModuleStage::BeginFrame).AddRaw(L, &FLayer::OnBeginFrame);
		Entry.FixedUpdate =
			GetPostStageDelegate(EModuleStage::FixedUpdate).AddRaw(L, &FLayer::OnFixedUpdate);
		Entry.Update =
			GetPostStageDelegate(EModuleStage::Update).AddRaw(L, &FLayer::OnUpdate);
		Entry.LateUpdate =
			GetPostStageDelegate(EModuleStage::LateUpdate).AddRaw(L, &FLayer::OnLateUpdate);
		Entry.PreRender =
			GetPostStageDelegate(EModuleStage::PreRender).AddRaw(L, &FLayer::OnPreRender);
		Entry.Render =
			GetPostStageDelegate(EModuleStage::Render).AddRaw(L, &FLayer::OnRender);
		Entry.PostRender =
			GetPostStageDelegate(EModuleStage::PostRender).AddRaw(L, &FLayer::OnPostRender);
		Entry.EndFrame =
			GetPostStageDelegate(EModuleStage::EndFrame).AddRaw(L, &FLayer::OnEndFrame);
	};

	for (FLayerBinding& Entry : LayerBindings)
	{
		BindForward(Entry);
	}

	// ProcessInput: overlays first (reverse of stack order).
	for (auto It = LayerBindings.rbegin(); It != LayerBindings.rend(); ++It)
	{
		It->ProcessInput = GetPostStageDelegate(EModuleStage::ProcessInput)
			.AddRaw(It->Layer.get(), &FLayer::OnProcessInput);
	}

	// Keep script Post hooks after layer bindings.
	if (FScriptModule* Script = GetModule<FScriptModule>())
	{
		if (Script->GetScriptSystem().IsInitialized())
		{
			Script->BindPostStageHooks(*this);
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
	Entry.Layer->OnAttach();
	LayerBindings.insert(
		LayerBindings.begin() + static_cast<std::ptrdiff_t>(LayerInsertIndex),
		std::move(Entry));
	++LayerInsertIndex;
	RebuildLayerPostBindings();
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
	Entry.Layer->OnAttach();
	LayerBindings.push_back(std::move(Entry));
	RebuildLayerPostBindings();
}

void FApp::ClearLayers()
{
	for (auto It = LayerBindings.rbegin(); It != LayerBindings.rend(); ++It)
	{
		UnbindLayerFromPostStages(*It);
		if (It->Layer)
		{
			It->Layer->OnDetach();
		}
	}
	LayerBindings.clear();
	LayerInsertIndex = 0;
}

bool FApp::Initialize()
{
	Configure(EngineConfig);

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

	if (FScriptModule* Script = GetModule<FScriptModule>())
	{
		Script->BindPostStageHooks(*this);
	}

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
