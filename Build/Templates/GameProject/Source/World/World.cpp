#include "World/World.h"

#include <Core/System/Log.h>

bool FWorld::Initialize(std::string InName)
{
	if (bInitialized)
	{
		return true;
	}

	Name = InName.empty() ? "MainWorld" : std::move(InName);
	TickCount = 0;
	bInitialized = true;
	MAHO_INFO("FWorld initialized (\"{}\")", Name);
	return true;
}

void FWorld::Tick(float /*DeltaSeconds*/)
{
	if (!bInitialized)
	{
		return;
	}
	++TickCount;
}

void FWorld::Shutdown()
{
	if (!bInitialized)
	{
		return;
	}

	MAHO_INFO("FWorld shut down (\"{}\", ticks={})", Name, TickCount);
	Name.clear();
	TickCount = 0;
	bInitialized = false;
}
