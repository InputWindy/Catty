#include "World/WorldLayer.h"

#include <Core/App.h>
#include <Core/Log.h>

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

void FWorldLayer::OnSequencerStage(Catty::EFrameStage Stage)
{
	if (Stage != Catty::EFrameStage::Update || !Catty::GApp)
	{
		return;
	}
	Catty::FStageContext Ctx{};
	Catty::GApp->MakeStageContext(Ctx);
	World.Tick(Ctx.DeltaSeconds);
}
