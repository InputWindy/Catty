#include "Catty/Core/Modules/ResourceModule.h"

#include "Catty/Core/App.h"
#include "Catty/Core/Log.h"
#include "Catty/Core/Modules/GCModule.h"
#include "Catty/Core/Modules/ScriptModule.h"
#include "Catty/Script/ScriptSystem.h"

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
			CATTY_CORE_ERROR("FResourceModule: GC module missing");
			return false;
		}
		if (!ResourceManager.Initialize(GC->GetGCManager()))
		{
			CATTY_CORE_ERROR("FResourceModule: Initialize failed");
			return false;
		}

		// Auto-bind when Lua becomes ready (Script Init broadcasts OnLuaReady).
		if (FScriptModule* ScriptMod = App.GetModule<FScriptModule>())
		{
			LuaReadyHandle = ScriptMod->GetScriptSystem().GetOnLuaReady().AddRaw(
				this,
				&FResourceModule::OnLuaReady);
		}
		return true;
	}

	case EModuleStage::Shutdown:
		if (LuaReadyHandle.IsValid())
		{
			if (FScriptModule* ScriptMod = App.GetModule<FScriptModule>())
			{
				ScriptMod->GetScriptSystem().GetOnLuaReady().Remove(LuaReadyHandle);
			}
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
