#include <Core/Layer/ScriptLayer.h>

#include <Core/App.h>
#include <Core/Log.h>

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

void FScriptLayer::OnSequencerStage(EFrameStage Stage)
{
	if (!Script.IsInitialized() || !GApp)
	{
		return;
	}
	FStageContext Ctx{};
	GApp->MakeStageContext(Ctx);
	if (Stage == EFrameStage::Update)
	{
		(void)Script.Call("OnUpdate", Ctx.DeltaSeconds);
	}
	else if (Stage == EFrameStage::FixedUpdate)
	{
		(void)Script.Call("OnFixedUpdate", Ctx.FixedDeltaSeconds);
	}
}

} // namespace Catty
