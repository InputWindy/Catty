#pragma once

#include "Catty/Core/Engine.h"
#include "Catty/Core/Export.h"
#include "Catty/Render/RenderServer.h"

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
 *           BeginFrame → ProcessInput → FixedUpdate(s) → Engine.Tick
 *           → Update → LateUpdate → [Flush render thread] → PreRender
 *           → Render → EndFrame
 * Shutdown: PreShutdown → Shutdown
 *
 * Render model: game thread enqueues work to FRenderServer; PreRender waits
 * until the render thread has drained all previously submitted tasks.
 *
 * Update hooks mirror Unity MonoBehaviour:
 *   FixedUpdate — fixed timestep (physics / deterministic sim), 0..N per frame
 *   Update      — once per rendered frame (gameplay)
 *   LateUpdate  — once per frame after Update (camera follow, post-logic)
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

	[[nodiscard]] float GetFixedDeltaSeconds() const { return FixedDeltaSeconds; }

protected:
	// ----- Startup / shutdown hooks (override as needed) -----

	/** Before Engine.Initialize: command line, working directory, logging, etc. */
	virtual bool PreInitialize();

	/** Fill EngineConfig (paths, ApplicationName, etc.). */
	virtual void Configure(FEngineConfig& OutConfig);

	/** After engine is ready: window, subsystems, load entry map (~UE StartGameInstance). */
	virtual bool PostInitialize();

	/** Tear down game-side resources before Shutdown. */
	virtual void PreShutdown();

	/** Default: stop render server + Engine.Shutdown. Overrides should call FApp::Shutdown. */
	virtual void Shutdown();

	// ----- Per-frame hooks (override as needed; do not override Tick) -----

	/** Frame start: pump messages, frame markers; no-op by default. */
	virtual void BeginFrame(float DeltaSeconds);

	/** Input polling (~UE ProcessLocalPlayerInput); no-op by default. */
	virtual void ProcessInput(float DeltaSeconds);

	/**
	 * Fixed-timestep update (~Unity FixedUpdate).
	 * Called 0..N times per frame with FixedDeltaSeconds; use for physics / sim.
	 */
	virtual void FixedUpdate(float FixedDeltaSeconds);

	/**
	 * Per-frame gameplay update (~Unity Update).
	 * Called once per frame with variable DeltaSeconds.
	 */
	virtual void Update(float DeltaSeconds);

	/**
	 * Post-Update hook (~Unity LateUpdate).
	 * Called once per frame after Update; use for camera follow, etc.
	 */
	virtual void LateUpdate(float DeltaSeconds);

	/**
	 * After render-thread Flush (PreRender sync). Safe to assume prior GPU/render tasks done.
	 * No-op by default.
	 */
	virtual void PreRender(float DeltaSeconds);

	/** Submit rendering (~RedrawViewports); no-op by default. */
	virtual void Render(float DeltaSeconds);

	/** Frame end: Present / stats; no-op by default. */
	virtual void EndFrame(float DeltaSeconds);

	/** Compute this frame's delta; fixed 1/60 for now, real clock later. */
	virtual float CalculateDeltaSeconds();

	FEngineConfig EngineConfig;
	FEngine Engine;
	FRenderServer RenderServer;

	/** Fixed step size for FixedUpdate (Unity default ~0.02s / 50Hz). */
	float FixedDeltaSeconds = 1.0f / 50.0f;

	/** Cap FixedUpdate steps per frame to avoid spiral-of-death on hitches. */
	int MaxFixedUpdatesPerFrame = 5;

	/**
	 * Temporary headless quit policy (owned by FApp, not game code).
	 * Until a platform window pumps WM_CLOSE / quit, Run() would spin forever;
	 * after AutoExitFrameCount frames, Tick calls RequestExit().
	 * Disable once windowing owns the exit path.
	 */
	bool bAutoExitAfterFrames = true;
	std::uint64_t AutoExitFrameCount = 3;

private:
	/** Non-virtual: Engine.Initialize + RenderServer.Initialize. */
	bool InitializeEngine();

	/**
	 * Fixed frame pipeline (not overridable).
	 * BeginFrame → ProcessInput → FixedUpdate(s) → Engine.Tick
	 * → Update → LateUpdate → Flush(render) → PreRender → Render → EndFrame
	 */
	void Tick(float DeltaSeconds);

	/** Drain FixedUpdateAccumulator; may invoke FixedUpdate 0..MaxFixedUpdatesPerFrame times. */
	void RunFixedUpdates(float DeltaSeconds);

	/** PreRender sync: wait for all tasks previously submitted to the render thread. */
	void FlushRenderServer();

	bool bRunning = false;
	float FixedUpdateAccumulator = 0.0f;
};

/**
 * Implemented once in the game EXE (not in Catty.dll).
 * Typically: return new FYourGameApp();
 */
FApp* CreateApplication();

} // namespace Catty
