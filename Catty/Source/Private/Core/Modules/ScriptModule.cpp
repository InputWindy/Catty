#include "Catty/Core/Modules/ScriptModule.h"

#include "Catty/Core/App.h"
#include "Catty/Core/Log.h"
#include "Catty/Core/Modules/ResourceModule.h"

#include <filesystem>

namespace Catty
{

bool FScriptModule::OnStage(EModuleStage Stage, FApp& App, FStageContext& Ctx)
{
	(void)Ctx;

	switch (Stage)
	{
	case EModuleStage::Init:
	{
		if (!ScriptSystem.Initialize(App.GetConfig().ProjectScriptsDir))
		{
			CATTY_CORE_ERROR("FScriptModule: Initialize failed");
			return false;
		}

		if (FResourceModule* Resource = App.GetModule<FResourceModule>())
		{
			ScriptSystem.BindResourceManager(Resource->GetResourceManager());
		}

		namespace fs = std::filesystem;
		const fs::path MainScript = fs::path(App.GetConfig().ProjectScriptsDir) / "main.lua";
		if (fs::is_regular_file(MainScript))
		{
			(void)ScriptSystem.DoFile("main.lua");
		}
		else
		{
			CATTY_CORE_INFO("FScriptSystem: no '{}' (skip)", MainScript.string());
		}
		return true;
	}

	case EModuleStage::Shutdown:
		if (UpdatePostHandle.IsValid())
		{
			App.GetPostStageDelegate(EModuleStage::Update).Remove(UpdatePostHandle);
			UpdatePostHandle = {};
		}
		if (FixedUpdatePostHandle.IsValid())
		{
			App.GetPostStageDelegate(EModuleStage::FixedUpdate).Remove(FixedUpdatePostHandle);
			FixedUpdatePostHandle = {};
		}
		if (ScriptSystem.IsInitialized())
		{
			ScriptSystem.Shutdown();
		}
		return true;

	default:
		return true;
	}
}

void FScriptModule::BindPostStageHooks(FApp& App)
{
	if (UpdatePostHandle.IsValid())
	{
		App.GetPostStageDelegate(EModuleStage::Update).Remove(UpdatePostHandle);
		UpdatePostHandle = {};
	}
	if (FixedUpdatePostHandle.IsValid())
	{
		App.GetPostStageDelegate(EModuleStage::FixedUpdate).Remove(FixedUpdatePostHandle);
		FixedUpdatePostHandle = {};
	}

	UpdatePostHandle = App.GetPostStageDelegate(EModuleStage::Update).AddLambda(
		[this](EModuleStage, FApp&, FStageContext& StageCtx)
		{
			if (ScriptSystem.IsInitialized())
			{
				(void)ScriptSystem.Call("OnUpdate", StageCtx.DeltaSeconds);
			}
		});

	FixedUpdatePostHandle = App.GetPostStageDelegate(EModuleStage::FixedUpdate).AddLambda(
		[this](EModuleStage, FApp&, FStageContext& StageCtx)
		{
			if (ScriptSystem.IsInitialized())
			{
				(void)ScriptSystem.Call("OnFixedUpdate", StageCtx.FixedDeltaSeconds);
			}
		});
}

} // namespace Catty
