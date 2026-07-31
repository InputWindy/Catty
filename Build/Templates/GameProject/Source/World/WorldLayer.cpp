#include "World/WorldLayer.h"

#include <Core/Application/App.h>
#include <Core/System/Log.h>

#include <utility>

FWorldLayer::FWorldLayer(std::string InWorldName)
	: Maho::FLayer("WorldLayer")
	, WorldName(std::move(InWorldName))
{
}

bool FWorldLayer::ExecuteStage(Maho::EEngineStage Stage)
{
	switch (Stage)
	{
	case Maho::EEngineStage::Attach:
		if (!bWorldReady)
		{
			if (!World.Initialize(WorldName))
			{
				MAHO_ERROR("FWorldLayer Attach: FWorld failed");
			}
			else
			{
				bWorldReady = true;
			}
		}
		break;
	case Maho::EEngineStage::Detach:
		if (bWorldReady)
		{
			World.Shutdown();
			bWorldReady = false;
		}
		break;
	case Maho::EEngineStage::Update:
		if (bWorldReady && Maho::GApp)
		{
			World.Tick(Maho::GApp->GetDeltaSeconds());
		}
		break;
	default:
		break;
	}
	return true;
}
