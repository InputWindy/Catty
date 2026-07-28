#pragma once

#include "Catty/Core/ConsoleManager.h"
#include "Catty/Core/Delegate.h"
#include "Catty/Core/Engine.h"
#include "Catty/Core/Export.h"
#include "Catty/Core/Layer.h"
#include "Catty/Core/Log.h"
#include "Catty/Core/Module.h"
#include "Catty/Core/Timer.h"
#include "Catty/Script/ScriptSystem.h"

#include <array>
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

/**
 * Empty stage pipeline. Game subclasses assemble Modules / Layers:
 *   RegisterModules() → Init stages → PostInitialize (PushLayer) → AttachModules → loop.
 */
class CATTY_API FApp
{
	// ---------------------------------------------------------------------------
	// Type aliases & lifetime
	// ---------------------------------------------------------------------------
public:
	CATTY_DECLARE_MULTICAST_DELEGATE_ThreeParams(
		FStageMulticast,
		EModuleStage,
		FApp&,
		FStageContext&);

	FApp();
	virtual ~FApp();

	FApp(const FApp&) = delete;
	FApp& operator=(const FApp&) = delete;

	// ---------------------------------------------------------------------------
	// Lifecycle
	// Run owns Initialize → main loop → Shutdown. Games override the hooks below.
	// ---------------------------------------------------------------------------
public:
	/** Sole public lifecycle entry. */
	void Run();

	[[nodiscard]] bool IsRunning() const { return bRunning; }

	/**
	 * Bound to Module / Layer multicasts (RegisterModule / PushLayer|Overlay).
	 * OnAttachLayer / OnDetachLayer also maintain PostStage bindings.
	 */
	void OnRequestExit();
	void OnAttachLayer(FLayer& Layer);
	void OnDetachLayer(FLayer& Layer);
	void OnAttachModule(IModule& Module);
	void OnDetachModule(IModule& Module);

protected:
	virtual void Configure(FEngineConfig& OutConfig);
	virtual void RegisterModules();
	virtual bool PostInitialize();
	virtual void PreShutdown();

private:
	bool Initialize();
	void Shutdown();
	void RunMainLoop();

	// ---------------------------------------------------------------------------
	// Core services
	// Hardcoded engine services (not Modules). Construction order:
	//   Log → ConsoleManager → Timer → ScriptSystem.
	// ---------------------------------------------------------------------------
public:
	[[nodiscard]] FEngineConfig& GetConfig() { return EngineConfig; }
	[[nodiscard]] const FEngineConfig& GetConfig() const { return EngineConfig; }

	[[nodiscard]] FLog& GetLog() { return Log; }
	[[nodiscard]] FConsoleManager& GetConsoleManager() { return ConsoleManager; }
	[[nodiscard]] FTimer& GetTimer() { return Timer; }
	[[nodiscard]] FScriptSystem& GetScriptSystem() { return ScriptSystem; }
	[[nodiscard]] const FScriptSystem& GetScriptSystem() const { return ScriptSystem; }

private:
	FEngineConfig EngineConfig;
	FLog Log;
	FConsoleManager ConsoleManager;
	FTimer Timer;
	FScriptSystem ScriptSystem;

	// ---------------------------------------------------------------------------
	// Modules
	// Fixed pipeline stage bodies (DAG). Register → topo order → OnStage / Attach.
	// ---------------------------------------------------------------------------
public:
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

private:
	bool RebuildModuleOrder();
	[[nodiscard]] const std::vector<IModule*>& GetOrderForStage(EModuleStage Stage) const;

	void AttachModules();
	void DetachModules();

	std::vector<std::unique_ptr<IModule>> Modules;
	std::unordered_map<std::type_index, IModule*> ModulesByType;
	std::unordered_map<std::string, IModule*> ModulesByName;
	std::vector<IModule*> StartupOrder;
	std::vector<IModule*> ShutdownOrder;

	// ---------------------------------------------------------------------------
	// Layers
	// Programmable content stack (World / Editor / Script). Post-bound on Attach.
	// ---------------------------------------------------------------------------
protected:
	void PushLayer(std::unique_ptr<FLayer> Layer);
	void PushOverlay(std::unique_ptr<FLayer> Overlay);
	void ClearLayers();

private:
	struct FLayerBinding
	{
		std::unique_ptr<FLayer> Layer;
	};

	std::vector<FLayerBinding> LayerBindings;
	std::size_t LayerInsertIndex = 0;

	// ---------------------------------------------------------------------------
	// Stage pipeline
	// Module OnStage then Layer Post multicast per EModuleStage.
	// ---------------------------------------------------------------------------
private:
	static constexpr std::size_t StageCount =
		static_cast<std::size_t>(EModuleStage::PostShutdown) + 1;

	bool ExecuteStage(EModuleStage Stage, FStageContext& Ctx);
	[[nodiscard]] FStageMulticast& GetPostStageDelegate(EModuleStage Stage);

	void RebuildLayerPostBindings();
	void RemoveLayerPostBindings(FLayer& Layer);
	void BindLayerPostBindings(FLayer& Layer);

	static bool IsInitFamily(EModuleStage Stage);
	static bool IsShutdownFamily(EModuleStage Stage);
	static std::size_t StageIndex(EModuleStage Stage);

	std::array<FStageMulticast, StageCount> PostStageDelegates{};

	// ---------------------------------------------------------------------------
	// Frame state
	// Per-frame timing; UpdateAppState() refreshes these each tick.
	// ---------------------------------------------------------------------------
private:
	/** Sample clock, write DeltaSeconds, advance FrameIndex / LastFrameTimeSeconds. */
	void UpdateAppState();

	bool bRunning = false;
	float DeltaSeconds = 0.0f;
	float FixedUpdateAccumulator = 0.0f;
	double LastFrameTimeSeconds = 0.0;
	std::uint64_t FrameIndex = 0;
};

/** Process-wide App while an FApp exists (set in FApp ctor, cleared in dtor). */
CATTY_API extern FApp* GApp;

FApp* CreateApplication();

} // namespace Catty
