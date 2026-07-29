#include <Core/Modules/ResourceModule.h>

#include <Core/App.h>
#include <Core/Log.h>

namespace Catty
{

bool FResourceModule::ExecuteStage(EModuleStage Stage, FApp& App, FStageContext& Ctx)
{
	(void)App;
	(void)Ctx;
	switch (Stage)
	{
	case EModuleStage::Init:
		if (!ResourceManager.Initialize())
		{
			CATTY_CORE_ERROR("FResourceModule: Initialize failed");
			return false;
		}
		return true;
	case EModuleStage::PrepareExit:
		ResourceManager.PrepareForExit();
		return true;
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

bool FResourceModule::IsIdle() const
{
	return ResourceManager.IsIdle();
}

} // namespace Catty
