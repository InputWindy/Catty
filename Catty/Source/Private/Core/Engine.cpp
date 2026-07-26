#include "Catty/Core/Engine.h"

#include <iostream>

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

	std::cout << "[Catty] Engine initialized\n"
			  << "  App            : " << Config.ApplicationName << '\n'
			  << "  EngineShaders  : " << Config.EngineShadersDir << '\n'
			  << "  ProjectShaders : " << Config.ProjectShadersDir << '\n'
			  << "  EnginePlugins  : " << Config.EnginePluginsDir << '\n'
			  << "  ProjectPlugins : " << Config.ProjectPluginsDir << '\n'
			  << "  Cached         : " << Config.CachedDir << '\n'
			  << "  Saved          : " << Config.SavedDir << '\n';

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

	std::cout << "[Catty] Engine shutdown after " << FrameIndex << " frames\n";
	bInitialized = false;
}

} // namespace Catty
