#include "Catty/World/World.h"

#include "Catty/Core/Log.h"

namespace Catty
{

bool FWorld::Initialize(std::string InName)
{
	if (bInitialized)
	{
		return true;
	}

	Name = InName.empty() ? "MainWorld" : std::move(InName);
	TickCount = 0;
	bInitialized = true;
	CATTY_CORE_INFO("FWorld initialized (\"{}\")", Name);
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

	CATTY_CORE_INFO("FWorld shut down (\"{}\", ticks={})", Name, TickCount);
	Name.clear();
	TickCount = 0;
	bInitialized = false;
}

} // namespace Catty
