#include "World/WorldLayer.h"

#include <Core/Application/App.h>
#include <Core/System/Log.h>

#include <utility>

FWorldLayer::FWorldLayer(std::string InWorldName)
	: Catty::FLayer("WorldLayer")
	, WorldName(std::move(InWorldName))
{
}

bool FWorldLayer::ExecuteStage(Catty::EEngineStage Stage)
{
	switch (Stage)
	{
	case Catty::EEngineStage::Attach:
		if (!bWorldReady)
		{
			if (!World.Initialize(WorldName))
			{
				CATTY_ERROR("FWorldLayer Attach: FWorld failed");
			}
			else
			{
				bWorldReady = true;
			}
		}
		break;
	case Catty::EEngineStage::Detach:
		if (bWorldReady)
		{
			World.Shutdown();
			bWorldReady = false;
		}
		break;
	case Catty::EEngineStage::Update:
		if (bWorldReady && Catty::GApp)
		{
			World.Tick(Catty::GApp->GetDeltaSeconds());
		}
		break;
	default:
		break;
	}
	return true;
}
