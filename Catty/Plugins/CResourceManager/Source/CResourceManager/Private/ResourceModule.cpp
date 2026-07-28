#include "ResourceModule.h"

#include "Core/App.h"
#include "Core/Log.h"
#include "GCModule.h"
#include "Script/ScriptSystem.h"

namespace Catty
{

void FResourceModule::OnLuaReady(FScriptSystem& Script)
{
	Script.Bind(ResourceManager);
}

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
			CATTY_CORE_ERROR("FResourceModule: CGCManager module missing");
			return false;
		}
		if (!ResourceManager.Initialize(GC->GetGCManager()))
		{
			CATTY_CORE_ERROR("FResourceModule: Initialize failed");
			return false;
		}

		LuaReadyHandle = App.GetScriptSystem().GetOnLuaReady().AddRaw(
			this,
			&FResourceModule::OnLuaReady);
		return true;
	}
	case EModuleStage::Shutdown:
		if (LuaReadyHandle.IsValid())
		{
			App.GetScriptSystem().GetOnLuaReady().Remove(LuaReadyHandle);
			LuaReadyHandle = {};
		}
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
