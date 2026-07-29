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
		if (!GC.IsInitialized())
		{
			CATTY_CORE_ERROR("FGCModule: FGC must be initialized");
			return false;
		}
		return true;
	case EModuleStage::Update:
		GC.Tick(Ctx.DeltaSeconds);
		return true;
	case EModuleStage::PrepareExit:
		// Drain aggressively while WaitForExit (intervals may be >0 at runtime).
		GC.CollectGarbage();
		GC.PurgePendingKill();
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

bool FGCModule::IsIdle() const
{
	return !GC.IsInitialized() || GC.IsIdle();
}

} // namespace Catty
