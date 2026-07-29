#include <Core/Modules/GCModule.h>

#include <Core/App.h>
#include <Core/Log.h>

namespace Catty
{

bool FGCModule::ExecuteStage(EModuleStage Stage, FApp& App, FStageContext& Ctx)
{
	(void)App;
	switch (Stage)
	{
	case EModuleStage::Init:
		if (!GC.Initialize())
		{
			CATTY_CORE_ERROR("FGCModule: Initialize failed");
			return false;
		}
		return true;
	case EModuleStage::Update:
		GC.Tick(Ctx.DeltaSeconds);
		return true;
	case EModuleStage::Shutdown:
		if (GC.IsInitialized())
		{
			GC.Shutdown();
		}
		return true;
	default:
		return true;
	}
}

} // namespace Catty
