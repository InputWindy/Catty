#include "Catty/Core/Modules/ResourceModule.h"

#include "Catty/Core/App.h"
#include "Catty/Core/Log.h"
#include "Catty/Core/Modules/GCModule.h"

namespace Catty
{

bool FResourceModule::OnStage(EModuleStage Stage, FApp& App, FStageContext& Ctx)
{
	(void)Ctx;

	switch (Stage)
	{
	case EModuleStage::Init:
	{
		FGCModule* GC = App.GetModule<FGCModule>();
		if (!GC)
		{
			CATTY_CORE_ERROR("FResourceModule: GC module missing");
			return false;
		}
		if (!ResourceManager.Initialize(GC->GetGCManager()))
		{
			CATTY_CORE_ERROR("FResourceModule: Initialize failed");
			return false;
		}
		return true;
	}

	case EModuleStage::Shutdown:
		if (ResourceManager.IsInitialized())
		{
			ResourceManager.Shutdown();
		}
		return true;

	default:
		return true;
	}
}

} // namespace Catty
