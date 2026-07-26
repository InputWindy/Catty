#pragma once

#include "Catty/Core/Engine.h"
#include "Catty/Core/Export.h"
#include "Catty/Core/Timer.h"
#include "Catty/Input/Input.h"
#include "Catty/Platform/PlatformWindow.h"
#include "Catty/Render/RenderServer.h"
#include "Catty/Resource/ResourceServer.h"

#include <cstdint>

namespace Catty
{

/**
 * Application base class (UE FEngineLoop + Unity PlayerLoop style).
 * Game projects inherit FApp and override lifecycle / frame hooks only.
 * Tick() is the fixed engine frame pipeline — not overridable.
 *
 * Startup:  PreInitialize → Configure → InitializeEngine → PostInitialize
 * Frame:    Tick() =
 *           PollEvents → BeginFrame → ProcessInput → FixedUpdate(s) → Engine.Tick
 *           → Update → LateUpdate → Flush(render) → PreRender → Render → EndFrame
 * Shutdown: PreShutdown → Shutdown
 */
class CATTY_API FApp
{
public:
	FApp();
	virtual ~FApp();

	FApp(const FApp&) = delete;
	FApp& operator=(const FApp&) = delete;

	/** Owned by EntryPoint: startup → frame loop → shutdown. */
	void Run();

	void RequestExit();
	[[nodiscard]] bool IsRunning() const { return bRunning; }

	[[nodiscard]] FEngine& GetEngine() { return Engine; }
	[[nodiscard]] const FEngine& GetEngine() const { return Engine; }

	[[nodiscard]] FRenderServer& GetRenderServer() { return RenderServer; }
	[[nodiscard]] const FRenderServer& GetRenderServer() const { return RenderServer; }

	[[nodiscard]] FResourceServer& GetResourceServer() { return ResourceServer; }
	[[nodiscard]] const FResourceServer& GetResourceServer() const { return ResourceServer; }

	[[nodiscard]] FTimer& GetTimer() { return Timer; }
	[[nodiscard]] const FTimer& GetTimer() const { return Timer; }

	[[nodiscard]] FInput& GetInput() { return Input; }
	[[nodiscard]] const FInput& GetInput() const { return Input; }

	[[nodiscard]] FPlatformWindow* GetPlatformWindow() { return PlatformWindow.get(); }
	[[nodiscard]] const FPlatformWindow* GetPlatformWindow() const { return PlatformWindow.get(); }

	[[nodiscard]] float GetFixedDeltaSeconds() const { return FixedDeltaSeconds; }

protected:
	virtual bool PreInitialize();
	virtual void Configure(FEngineConfig& OutConfig);
	virtual bool PostInitialize();
	virtual void PreShutdown();

	/** Default: render server → resource server → platform → engine. */
	virtual void Shutdown();

	virtual void BeginFrame(float DeltaSeconds);

	/** Default: update FInput; Escape requests exit. */
	virtual void ProcessInput(float DeltaSeconds);

	virtual void FixedUpdate(float FixedDeltaSeconds);
	virtual void Update(float DeltaSeconds);
	virtual void LateUpdate(float DeltaSeconds);
	virtual void PreRender(float DeltaSeconds);

	/** Default: enqueue clear/present on the render server when RHI is alive. */
	virtual void Render(float DeltaSeconds);

	virtual void EndFrame(float DeltaSeconds);
	virtual float CalculateDeltaSeconds();

	FEngineConfig EngineConfig;
	FEngine Engine;
	FRenderServer RenderServer;
	FResourceServer ResourceServer;
	FTimer Timer;
	FInput Input;
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
