#include "Catty/Core/Engine.h"
#include "Catty/Core/Log.h"

namespace Catty
{

FEngine::FEngine() = default;

FEngine::~FEngine()
{
	if (bInitialized)
	{
		Shutdown();
	}
}

bool FEngine::Initialize(const FEngineConfig& InConfig)
{
	if (bInitialized)
	{
		return true;
	}

	Config = InConfig;
	FrameIndex = 0;
	bInitialized = true;

	CATTY_CORE_INFO("Engine initialized");
	CATTY_CORE_INFO("  App            : {}", Config.ApplicationName);
	CATTY_CORE_INFO("  EngineShaders  : {}", Config.EngineShadersDir);
	CATTY_CORE_INFO("  ProjectShaders : {}", Config.ProjectShadersDir);
	CATTY_CORE_INFO("  EnginePlugins  : {}", Config.EnginePluginsDir);
	CATTY_CORE_INFO("  ProjectPlugins : {}", Config.ProjectPluginsDir);
	CATTY_CORE_INFO("  Cached         : {}", Config.CachedDir);
	CATTY_CORE_INFO("  Saved          : {}", Config.SavedDir);
	CATTY_CORE_INFO("  ProjectConfig  : {}", Config.ProjectConfigDir);

	return true;
}

void FEngine::Tick(float /*DeltaSeconds*/)
{
	if (!bInitialized)
	{
		return;
	}

	++FrameIndex;
}

void FEngine::Shutdown()
{
	if (!bInitialized)
	{
		return;
	}

	CATTY_CORE_INFO("Engine shutdown after {} frames", FrameIndex);
	bInitialized = false;
}

} // namespace Catty
