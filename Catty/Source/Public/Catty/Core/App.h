#pragma once

#include "Catty/Core/Engine.h"
#include "Catty/Core/Export.h"
#include "Catty/Core/Layer.h"
#include "Catty/Core/LayerStack.h"
#include "Catty/Core/Timer.h"
#include "Catty/Platform/PlatformWindow.h"
#include "Catty/Render/RenderServer.h"
#include "Catty/Resource/GCManager.h"
#include "Catty/Resource/ResourceManager.h"
#include "Catty/Resource/ResourceServer.h"
#include "Catty/UI/ImGuiSystem.h"

#include <cstdint>
#include <memory>

namespace Catty
{

/**
 * Application base class (UE FEngineLoop + Unity PlayerLoop style).
 * Game projects inherit FApp and override lifecycle / frame hooks only.
 * Tick() is the fixed engine frame pipeline — not overridable.
 * Per-frame work is also composable via FLayerStack (PushLayer / PushOverlay).
 *
 * Example:
 * ```
 *   class FMyGameApp : public Catty::FApp
 *   {
 *   protected:
 *       virtual void Configure(Catty::FEngineConfig& OutConfig) override
 *       {
 *           OutConfig.ApplicationName = "MyGame";
 *           OutConfig.ProjectConfigDir = "Config";
 *       }
 *       virtual bool PostInitialize() override
 *       {
 *           PushLayer(std::make_unique<FWorldLayer>());
 *           return true;
 *       }
 *   };
 *   // + Catty::CreateApplication() returning new FMyGameApp (see EntryPoint.h)
 * ```
 */
class CATTY_API FApp
{
public:
	FApp();
	virtual ~FApp();

	FApp(const FApp&) = delete;
	FApp& operator=(const FApp&) = delete;

	void Run();

	void RequestExit();
	[[nodiscard]] bool IsRunning() const { return bRunning; }

	[[nodiscard]] FEngine& GetEngine() { return Engine; }
	[[nodiscard]] const FEngine& GetEngine() const { return Engine; }

	[[nodiscard]] FRenderServer& GetRenderServer() { return RenderServer; }
	[[nodiscard]] const FRenderServer& GetRenderServer() const { return RenderServer; }

	[[nodiscard]] FResourceServer& GetResourceServer() { return ResourceServer; }
	[[nodiscard]] const FResourceServer& GetResourceServer() const { return ResourceServer; }

	[[nodiscard]] FResourceManager& GetResourceManager() { return ResourceManager; }
	[[nodiscard]] const FResourceManager& GetResourceManager() const { return ResourceManager; }

	[[nodiscard]] FGCManager& GetGCManager() { return GCManager; }
	[[nodiscard]] const FGCManager& GetGCManager() const { return GCManager; }

	[[nodiscard]] FTimer& GetTimer() { return Timer; }
	[[nodiscard]] const FTimer& GetTimer() const { return Timer; }

	[[nodiscard]] FImGuiSystem& GetImGui() { return ImGui; }
	[[nodiscard]] const FImGuiSystem& GetImGui() const { return ImGui; }

	[[nodiscard]] FLayerStack& GetLayerStack() { return LayerStack; }
	[[nodiscard]] const FLayerStack& GetLayerStack() const { return LayerStack; }

	[[nodiscard]] FPlatformWindow* GetPlatformWindow() { return PlatformWindow.get(); }
	[[nodiscard]] const FPlatformWindow* GetPlatformWindow() const { return PlatformWindow.get(); }

	[[nodiscard]] float GetFixedDeltaSeconds() const { return FixedDeltaSeconds; }

	void PushLayer(std::unique_ptr<FLayer> Layer);
	void PushOverlay(std::unique_ptr<FLayer> Overlay);

protected:
	virtual bool PreInitialize();
	virtual void Configure(FEngineConfig& OutConfig);
	virtual bool PostInitialize();
	virtual void PreShutdown();

	/** Default: clear layers → ImGui → render → resource manager → resource server → platform → engine. */
	virtual void Shutdown();

	/** Default: ImGui NewFrame, then layer stack. */
	virtual void BeginFrame(float DeltaSeconds);

	/**
	 * Default: Escape via ImGui IO requests exit (unless WantCaptureKeyboard),
	 * then layer stack (overlays first).
	 */
	virtual void ProcessInput(float DeltaSeconds);

	virtual void FixedUpdate(float FixedDeltaSeconds);

	/** Default: dispatches to the layer stack. */
	virtual void Update(float DeltaSeconds);

	virtual void LateUpdate(float DeltaSeconds);
	virtual void PreRender(float DeltaSeconds);

	/** Default: layer stack, then ImGui EndFrame + clear/present enqueue. */
	virtual void Render(float DeltaSeconds);

	virtual void EndFrame(float DeltaSeconds);
	virtual float CalculateDeltaSeconds();

	FEngineConfig EngineConfig;
	FEngine Engine;
	FRenderServer RenderServer;
	FResourceServer ResourceServer;
	FGCManager GCManager;
	FResourceManager ResourceManager;
	FTimer Timer;
	FImGuiSystem ImGui;
	FLayerStack LayerStack;
	FPlatformWindowPtr PlatformWindow;

	float FixedDeltaSeconds = 1.0f / 50.0f;
	int MaxFixedUpdatesPerFrame = 5;

	bool bAutoExitAfterFrames = false;
	std::uint64_t AutoExitFrameCount = 3;

private:
	bool InitializeEngine();
	void Tick(float DeltaSeconds);
	void RunFixedUpdates(float DeltaSeconds);
	void FlushRenderServer();
	void SyncFramebufferSize();

	bool bRunning = false;
	float FixedUpdateAccumulator = 0.0f;
	double LastFrameTimeSeconds = 0.0;
	int LastFramebufferWidth = 0;
	int LastFramebufferHeight = 0;
};

FApp* CreateApplication();

} // namespace Catty
