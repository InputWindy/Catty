#pragma once

#include "Catty/Core/Export.h"
#include "Catty/Platform/PlatformWindow.h"

#include <cstdint>
#include <string>

namespace Catty
{

struct FEngineConfig
{
	std::string ApplicationName = "CattyApp";
	std::string EngineShadersDir = "Engine/Shaders";
	std::string ProjectShadersDir = "Project/Shaders";
	std::string EnginePluginsDir = "Engine/Plugins";
	std::string ProjectPluginsDir = "Project/Plugins";
	/** UE-style: regenerable derived data (shader cache, etc.). */
	std::string CachedDir = "Cached";
	/** UE-style: logs / config / crashes / screenshots. */
	std::string SavedDir = "Saved";
	/** Project Config/ directory (DefaultEngine.ini, etc.). */
	std::string ProjectConfigDir = "Config";
	/** Project Scripts/ directory (Lua game logic). */
	std::string ProjectScriptsDir = "Scripts";

	/** Main window. Ignored when bCreateMainWindow is false. */
	EPlatform Platform = EPlatform::Glfw;
	int WindowWidth = 1280;
	int WindowHeight = 720;
	bool bCreateMainWindow = true;
	bool bResizableWindow = true;

	/** Clear color used by the default clear/present path (Render / ImGui PostRender). */
	float ClearColorR = 0.08f;
	float ClearColorG = 0.10f;
	float ClearColorB = 0.16f;
	float ClearColorA = 1.0f;
};

/**
 * Core engine bookkeeping (config + frame index).
 * Gameplay worlds live in project layers, not here.
 *
 * Example:
 * ```
 *   Catty::FEngineConfig Config;
 *   Config.ApplicationName = "MyGame";
 *   Config.ProjectConfigDir = "Config";
 *
 *   Catty::FEngine Engine;
 *   Engine.Initialize(Config);
 *   Engine.Tick(DeltaSeconds);
 *   const std::uint64_t Frame = Engine.GetFrameIndex();
 *   Engine.Shutdown();
 * ```
 */
class CATTY_API FEngine
{
public:
	FEngine();
	~FEngine();

	FEngine(const FEngine&) = delete;
	FEngine& operator=(const FEngine&) = delete;

	bool Initialize(const FEngineConfig& Config);
	void Tick(float DeltaSeconds);
	void Shutdown();

	[[nodiscard]] bool IsInitialized() const { return bInitialized; }
	[[nodiscard]] std::uint64_t GetFrameIndex() const { return FrameIndex; }
	[[nodiscard]] const FEngineConfig& GetConfig() const { return Config; }

private:
	bool bInitialized = false;
	std::uint64_t FrameIndex = 0;
	FEngineConfig Config{};
};

} // namespace Catty
