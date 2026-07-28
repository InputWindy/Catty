#include "Catty/Core/Modules/WorkerModule.h"

#include "Catty/Core/App.h"
#include "Catty/Core/Log.h"

namespace Catty
{

bool FWorkerModule::OnStage(EModuleStage Stage, FApp& App, FStageContext& Ctx)
{
	(void)App;
	(void)Ctx;

	switch (Stage)
	{
	case EModuleStage::Init:
		if (!WorkerPool.Initialize())
		{
			CATTY_CORE_ERROR("FWorkerModule: Initialize failed");
			return false;
		}
		return true;

	case EModuleStage::Shutdown:
		if (WorkerPool.IsInitialized())
		{
			WorkerPool.Shutdown();
		}
		return true;

	default:
		return true;
	}
}

} // namespace Catty
