#pragma once

#include "Catty/Core/Delegate.h"
#include "Catty/Core/Engine.h"
#include "Catty/Core/Export.h"
#include "Catty/Core/Layer.h"
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
public:
	using FStageMulticast = TMulticastDelegate<void(EModuleStage, FApp&, FStageContext&)>;

	FApp();
	virtual ~FApp();

	FApp(const FApp&) = delete;
	FApp& operator=(const FApp&) = delete;

	/** Sole public lifecycle entry: private Initialize → loop → Shutdown. */
	void Run();

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

	[[nodiscard]] FEngineConfig& GetConfig() { return EngineConfig; }
	[[nodiscard]] const FEngineConfig& GetConfig() const { return EngineConfig; }

	[[nodiscard]] FTimer& GetTimer() { return Timer; }

	/**
	 * Lua VM host (empty until a FScriptLayer Attach initializes it).
	 * Not a default Layer — games Push FScriptLayer when they want scripting.
	 */
	[[nodiscard]] FScriptSystem& GetScriptSystem() { return ScriptSystem; }
	[[nodiscard]] const FScriptSystem& GetScriptSystem() const { return ScriptSystem; }

	/** Game / tools may request exit; modules Broadcast IModule::OnExitRequested instead. */
	void RequestExit();
	[[nodiscard]] bool IsRunning() const { return bRunning; }

protected:
	virtual void Configure(FEngineConfig& OutConfig);

	/** Assemble IModule graph. Default empty — games typically call RegisterEngineModules. */
	virtual void RegisterModules();

	virtual bool PostInitialize();
	virtual void PreShutdown();

	/**
	 * Owns Layer lifetime. Binds On* to PostStageDelegates; does not tick Layers.
	 * Layer section then Overlay section (overlays sit above layers).
	 */
	void PushLayer(std::unique_ptr<FLayer> Layer);
	void PushOverlay(std::unique_ptr<FLayer> Overlay);
	void ClearLayers();

private:
	struct FLayerBinding
	{
		std::unique_ptr<FLayer> Layer;
		bool bOverlay = false;
	};

	static constexpr std::size_t StageCount =
		static_cast<std::size_t>(EModuleStage::PostShutdown) + 1;

	bool Initialize();
	void Shutdown();
	void RunMainLoop();
	bool ExecuteStage(EModuleStage Stage, FStageContext& Ctx);

	bool RebuildModuleOrder();
	[[nodiscard]] const std::vector<IModule*>& GetOrderForStage(EModuleStage Stage) const;
	float CalculateDeltaSeconds();
	static bool IsInitFamily(EModuleStage Stage);
	static bool IsShutdownFamily(EModuleStage Stage);
	static std::size_t StageIndex(EModuleStage Stage);

	void AttachModules();
	void DetachModules();
	void HandleModuleAttach(IModule& Module);
	void HandleModuleDetach(IModule& Module);

	void HandleLayerAttach(FLayer& Layer);
	void HandleLayerDetach(FLayer& Layer);
	void RebuildLayerPostBindings();
	void RemoveLayerPostBindings(FLayer& Layer);
	void BindLayerPostBindings(FLayer& Layer);

	[[nodiscard]] FStageMulticast& GetPostStageDelegate(EModuleStage Stage);

	FEngineConfig EngineConfig;
	FTimer Timer;
	FScriptSystem ScriptSystem;

	std::vector<std::unique_ptr<IModule>> Modules;
	std::unordered_map<std::type_index, IModule*> ModulesByType;
	std::unordered_map<std::string, IModule*> ModulesByName;
	std::vector<IModule*> StartupOrder;
	std::vector<IModule*> ShutdownOrder;

	std::array<FStageMulticast, StageCount> PostStageDelegates{};

	/** Layer ownership only — Post binds live on PostStageDelegates. */
	std::vector<FLayerBinding> LayerBindings;
	std::size_t LayerInsertIndex = 0;

	bool bRunning = false;
	bool bModuleOrderBuilt = false;
	float FixedUpdateAccumulator = 0.0f;
	double LastFrameTimeSeconds = 0.0;
	std::uint64_t FrameIndex = 0;
};

FApp* CreateApplication();

} // namespace Catty
