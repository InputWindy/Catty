#include "Catty/Script/ScriptLayer.h"

#include "Catty/Core/Log.h"

#include <filesystem>

namespace Catty
{

FScriptLayer::FScriptLayer(FScriptSystem& InScript, std::string InScriptsDirectory)
	: FLayer("Script")
	, Script(InScript)
	, ScriptsDirectory(std::move(InScriptsDirectory))
{
}

void FScriptLayer::OnAttach()
{
	if (!Script.Initialize(ScriptsDirectory))
	{
		CATTY_CORE_ERROR("FScriptLayer: Initialize failed");
		return;
	}

	namespace fs = std::filesystem;
	const fs::path MainScript = fs::path(ScriptsDirectory) / "main.lua";
	if (fs::is_regular_file(MainScript))
	{
		(void)Script.DoFile("main.lua");
	}
	else
	{
		CATTY_CORE_INFO("FScriptSystem: no '{}' (skip)", MainScript.string());
	}
}

void FScriptLayer::OnDetach()
{
	if (Script.IsInitialized())
	{
		Script.Shutdown();
	}
}

void FScriptLayer::OnUpdate(EModuleStage Stage, FApp& App, FStageContext& Ctx)
{
	(void)Stage;
	(void)App;
	if (Script.IsInitialized())
	{
		(void)Script.Call("OnUpdate", Ctx.DeltaSeconds);
	}
}

void FScriptLayer::OnFixedUpdate(EModuleStage Stage, FApp& App, FStageContext& Ctx)
{
	(void)Stage;
	(void)App;
	if (Script.IsInitialized())
	{
		(void)Script.Call("OnFixedUpdate", Ctx.FixedDeltaSeconds);
	}
}

} // namespace Catty
