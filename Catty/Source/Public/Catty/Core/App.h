#pragma once

#include "Catty/Core/Engine.h"
#include "Catty/Core/Export.h"

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
 *           → Update → LateUpdate → Render → EndFrame
 * Shutdown: PreShutdown → Shutdown
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

	/** Default: Engine.Shutdown. Overrides should call FApp::Shutdown. */
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

	/** Submit rendering (~RedrawViewports); no-op by default. */
	virtual void Render(float DeltaSeconds);

	/** Frame end: Present / stats; no-op by default. */
	virtual void EndFrame(float DeltaSeconds);

	/** Compute this frame's delta; fixed 1/60 for now, real clock later. */
	virtual float CalculateDeltaSeconds();

	FEngineConfig EngineConfig;
	FEngine Engine;

	/** Fixed step size for FixedUpdate (Unity default ~0.02s / 50Hz). */
	float FixedDeltaSeconds = 1.0f / 50.0f;

	/** Cap FixedUpdate steps per frame to avoid spiral-of-death on hitches. */
	int MaxFixedUpdatesPerFrame = 5;

private:
	/** Non-virtual: only Engine.Initialize, so subclasses cannot skip it. */
	bool InitializeEngine();

	/**
	 * Fixed frame pipeline (not overridable).
	 * BeginFrame → ProcessInput → FixedUpdate(s) → Engine.Tick
	 * → Update → LateUpdate → Render → EndFrame
	 */
	void Tick(float DeltaSeconds);

	/** Drain FixedUpdateAccumulator; may invoke FixedUpdate 0..MaxFixedUpdatesPerFrame times. */
	void RunFixedUpdates(float DeltaSeconds);

	bool bRunning = false;
	float FixedUpdateAccumulator = 0.0f;
};

/**
 * Implemented once in the game EXE (not in Catty.dll).
 * Typically: return new FYourGameApp();
 */
FApp* CreateApplication();

} // namespace Catty
