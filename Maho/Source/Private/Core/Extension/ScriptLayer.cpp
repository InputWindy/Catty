#include <Core/Extension/ScriptLayer.h>

#include <Core/Application/App.h>
#include <Core/Extension/Script.h>
#include <Core/System/Log.h>

#include <filesystem>

namespace Maho
{

FScriptLayer::FScriptLayer()
	: FLayer("Script")
{
}

FScriptSystem* FScriptLayer::TryGetScript() const
{
	if (!GApp)
	{
		return nullptr;
	}
	return GApp->GetExtension<FScriptSystem>();
}

bool FScriptLayer::ExecuteStage(EEngineStage Stage)
{
	FScriptSystem* Script = TryGetScript();
	if (!Script || !Script->IsLuaInitialized())
	{
		return true;
	}

	switch (Stage)
	{
	case EEngineStage::Attach:
	{
		namespace fs = std::filesystem;
		const fs::path MainScript = fs::path(Script->GetScriptsDirectory()) / "main.lua";
		if (fs::is_regular_file(MainScript))
		{
			(void)Script->DoFile("main.lua");
		}
		else
		{
			MAHO_CORE_INFO("FScriptLayer: no '{}' (skip)", MainScript.string());
		}
		break;
	}
	case EEngineStage::Update:
		if (GApp)
		{
			(void)Script->Call("OnUpdate", GApp->GetDeltaSeconds());
		}
		break;
	case EEngineStage::FixedUpdate:
		if (GApp)
		{
			(void)Script->Call("OnFixedUpdate", GApp->GetFixedDeltaSeconds());
		}
		break;
	default:
		break;
	}
	return true;
}

} // namespace Maho
