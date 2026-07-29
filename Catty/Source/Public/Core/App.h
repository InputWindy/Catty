#pragma once

#include <Core/AppFrameSequencerTraits.h>
#include <Core/ConsoleManager.h>
#include <Core/Engine.h>
#include <Core/Export.h>
#include <Core/Layer.h>
#include <Core/Log.h>
#include <Core/Module.h>
#include <Core/SequenceGraph.h>
#include <Core/Timer.h>
#include <Core/WorkerPool.h>
#include <Core/Layer/ScriptSystem.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace Catty
{

/** Alias to SequenceGraph state (same values as former EAppState). */
using EAppState = ESequenceGraphState;

/**
 * Application shell: Modules (Init/Shutdown + frame Sequencer A) and Layers (Sequencer B).
 *   RegisterModules() → Init stages → PostInitialize (PushLayer) → AttachModules → Execute().
 */
class CATTY_API FApp : public TSequenceGraph<FApp, FModuleFrameTraits, FLayerFrameTraits>
{
public:
	FApp();
	~FApp() override;

	FApp(const FApp&) = delete;
	FApp& operator=(const FApp&) = delete;

	void Run();

	[[nodiscard]] bool IsRunning() const
	{
		return TSequenceGraph::IsRunning();
	}

	[[nodiscard]] EAppState GetState() const
	{
		return TSequenceGraph::GetState();
	}

	void OnRequestExit();

	[[nodiscard]] FEngineConfig& GetConfig() { return EngineConfig; }
	[[nodiscard]] const FEngineConfig& GetConfig() const { return EngineConfig; }

	[[nodiscard]] FLog& GetLog() { return Log; }
	[[nodiscard]] FConsoleManager& GetConsoleManager() { return ConsoleManager; }
	[[nodiscard]] FTimer& GetTimer() { return Timer; }
	[[nodiscard]] FWorkerPool& GetWorkerPool() { return WorkerPool; }
	[[nodiscard]] const FWorkerPool& GetWorkerPool() const { return WorkerPool; }
	[[nodiscard]] FScriptSystem& GetScriptSystem() { return ScriptSystem; }
	[[nodiscard]] const FScriptSystem& GetScriptSystem() const { return ScriptSystem; }

	[[nodiscard]] std::uint64_t GetFrameIndex() const { return FrameIndex; }
	[[nodiscard]] float GetDeltaSeconds() const { return DeltaSeconds; }
	[[nodiscard]] float GetFixedDeltaSeconds() const { return FixedDeltaSeconds; }

	void MakeStageContext(FStageContext& Out) const;
	void UpdateAppState();
	[[nodiscard]] bool AreAllModulesIdle() const;

	void RegisterModule(std::unique_ptr<IModule> Module);

	template <typename T>
	T* GetModule()
	{
		static_assert(std::is_base_of_v<IModule, T>, "T must derive from IModule");
		const auto It = ModulesByType.find(std::type_index(typeid(T)));
		return It != ModulesByType.end() ? static_cast<T*>(It->second) : nullptr;
	}

	template <typename T>
	const T* GetModule() const
	{
		return const_cast<FApp*>(this)->GetModule<T>();
	}

	[[nodiscard]] IModule* GetModuleByName(const char* Name);
	[[nodiscard]] const IModule* GetModuleByName(const char* Name) const;

protected:
	virtual void Configure(FEngineConfig& OutConfig);
	virtual void RegisterModules();
	virtual bool PostInitialize();

	void PushLayer(std::unique_ptr<FLayer> Layer);
	void PushOverlay(std::unique_ptr<FLayer> Overlay);
	void ClearLayers();

	[[nodiscard]] bool BuildGraph() override;

private:
	bool Initialize();
	void Shutdown();
	void InstallFixedUpdateRepeat();
	void BootstrapFirstAttach();
	void OnWorkersStarted() override;

	void OnDetachModule(IModule& Module);

	bool RebuildModuleOrder();
	[[nodiscard]] bool BuildStageOrder(EModuleStage Stage, std::vector<IModule*>& OutOrder);
	[[nodiscard]] const std::vector<IModule*>& GetOrderForStage(EModuleStage Stage) const;
	[[nodiscard]] bool WaitModuleDependencies(IModule& Module, EModuleStage Stage);

	void AttachModules();
	void DetachModules();

	bool ExecuteLifecycleStage(EModuleStage Stage, FStageContext& Ctx);
	static bool IsLifecycleStage(EModuleStage Stage);
	static bool IsInitFamily(EModuleStage Stage);
	static std::size_t StageIndex(EModuleStage Stage);

	FEngineConfig EngineConfig;
	FLog Log;
	FConsoleManager ConsoleManager;
	FTimer Timer;
	FWorkerPool WorkerPool;
	FScriptSystem ScriptSystem;

	std::vector<std::unique_ptr<IModule>> Modules;
	std::unordered_map<std::type_index, IModule*> ModulesByType;
	std::unordered_map<std::string, IModule*> ModulesByName;
	std::array<std::vector<IModule*>, static_cast<std::size_t>(EModuleStage::NumMaxStage)> StageOrders{};

	std::vector<std::unique_ptr<FLayer>> Layers;
	std::size_t LayerInsertIndex = 0;

	float DeltaSeconds = 0.0f;
	float FixedDeltaSeconds = 0.0f;
	float FixedUpdateAccumulator = 0.0f;
	int FixedStepsRemaining = 0;
	std::atomic<bool> bFrameTickArmed{true};
	double LastFrameTimeSeconds = 0.0;
	std::uint64_t FrameIndex = 0;
};

CATTY_API extern FApp* GApp;

FApp* CreateApplication();

} // namespace Catty
