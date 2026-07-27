#include "World/WorldLayer.h"

#include <Catty/Core/Log.h>

#include <utility>

FWorldLayer::FWorldLayer(std::string InWorldName)
	: Catty::FLayer("WorldLayer")
	, WorldName(std::move(InWorldName))
{
}

void FWorldLayer::OnAttach()
{
	if (!World.Initialize(WorldName))
	{
		CATTY_ERROR("FWorldLayer::OnAttach: FWorld failed");
	}
}

void FWorldLayer::OnDetach()
{
	World.Shutdown();
}

void FWorldLayer::OnUpdate(float DeltaSeconds)
{
	World.Tick(DeltaSeconds);
}
