#pragma once

#include "Catty/Core/Engine.h"
#include "Catty/Core/Export.h"

namespace Catty
{

/**
 * Application base class (UE GameInstance / Hazel Application style).
 * Game projects inherit FApp and override the lifecycle hooks.
 * main() lives in EntryPoint.h and drives Run().
 */
class CATTY_API FApp
{
public:
	FApp();
	virtual ~FApp();

	FApp(const FApp&) = delete;
	FApp& operator=(const FApp&) = delete;

	/** Owned by EntryPoint: Initialize → Tick loop → Shutdown. */
	void Run();

	void RequestExit();
	[[nodiscard]] bool IsRunning() const { return bRunning; }

	[[nodiscard]] FEngine& GetEngine() { return Engine; }
	[[nodiscard]] const FEngine& GetEngine() const { return Engine; }

protected:
	/** Override to fill EngineConfig before base Initialize. */
	virtual void Configure(FEngineConfig& OutConfig);

	virtual bool Initialize();
	virtual void Tick(float DeltaSeconds);
	virtual void Shutdown();

	FEngineConfig EngineConfig;
	FEngine Engine;

private:
	bool bRunning = false;
};

/**
 * Implemented once in the game EXE (not in Catty.dll).
 * Typically: return new FYourGameApp();
 */
FApp* CreateApplication();

} // namespace Catty
