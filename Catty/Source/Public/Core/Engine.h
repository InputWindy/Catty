#pragma once

#include <Core/Export.h>
#include <Core/PlatformWindow.h>

#include <string>

namespace Catty
{

/**
 * App / module configuration owned by FApp (not a separate FEngine instance).
 * Paths may be relative until FPaths::Initialize absolutizes them from project/engine roots.
 */
struct FEngineConfig
{
	std::string ApplicationName = "CattyApp";

	/** Absolute or relative roots filled by FPaths::Initialize (and/or Configure). */
	std::string ProjectDir;
	std::string EngineDir;

	std::string EngineShadersDir = "Engine/Shaders";
	std::string ProjectShadersDir = "Project/Shaders";
	std::string EnginePluginsDir = "Engine/Plugins";
	std::string ProjectPluginsDir = "Project/Plugins";
	std::string ProjectContentDir = "Content";
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

} // namespace Catty
