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

	/** Main window. Ignored when bCreateMainWindow is false. */
	EPlatform Platform = EPlatform::Glfw;
	int WindowWidth = 1280;
	int WindowHeight = 720;
	bool bCreateMainWindow = true;
	bool bResizableWindow = true;
};

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
