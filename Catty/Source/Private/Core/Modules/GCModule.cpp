#include "Catty/Core/Modules/GCModule.h"

#include "Catty/Core/App.h"
#include "Catty/Core/Log.h"

namespace Catty
{

bool FGCModule::OnStage(EModuleStage Stage, FApp& App, FStageContext& Ctx)
{
	(void)App;

	switch (Stage)
	{
	case EModuleStage::Init:
		if (!GCManager.Initialize())
		{
			CATTY_CORE_ERROR("FGCModule: Initialize failed");
			return false;
		}
		return true;

	case EModuleStage::Update:
		GCManager.Tick(Ctx.DeltaSeconds);
		return true;

	case EModuleStage::Shutdown:
		if (GCManager.IsInitialized())
		{
			GCManager.Shutdown();
		}
		return true;

	default:
		return true;
	}
}

} // namespace Catty
