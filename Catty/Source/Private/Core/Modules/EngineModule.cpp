#include "Catty/Core/Modules/EngineModule.h"

#include "Catty/Core/App.h"
#include "Catty/Core/Log.h"

namespace Catty
{

bool FEngineModule::OnStage(EModuleStage Stage, FApp& App, FStageContext& Ctx)
{
	switch (Stage)
	{
	case EModuleStage::Init:
		if (!Engine.Initialize(App.GetConfig()))
		{
			CATTY_CORE_ERROR("FEngineModule: Initialize failed");
			return false;
		}
		return true;

	case EModuleStage::Update:
		Engine.Tick(Ctx.DeltaSeconds);
		Ctx.FrameIndex = Engine.GetFrameIndex();
		return true;

	case EModuleStage::Shutdown:
		if (Engine.IsInitialized())
		{
			Engine.Shutdown();
		}
		return true;

	default:
		return true;
	}
}

} // namespace Catty
