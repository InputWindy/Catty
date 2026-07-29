#include <Catty.h>
#include <EntryPoint.h>
#include <Core/Layer/ScriptLayer.h>

#include "World/WorldLayer.h"

#include <memory>

class {{APP_CLASS}} : public Catty::FApp
{
protected:
	virtual void Configure(Catty::FEngineConfig& OutConfig) override
	{
		OutConfig.ApplicationName = "{{PROJECT_NAME}}";
		OutConfig.EngineShadersDir = "Engine/Shaders";
		OutConfig.ProjectShadersDir = "Project/Shaders";
		OutConfig.EnginePluginsDir = "Engine/Plugins";
		OutConfig.ProjectPluginsDir = "Project/Plugins";
		OutConfig.CachedDir = "Cached";
		OutConfig.SavedDir = "Saved";
		OutConfig.ProjectConfigDir = "Config";
	}

	virtual bool PostInitialize() override
	{
		PushLayer(std::make_unique<FWorldLayer>("MainWorld"));

#if defined(GAME_WITH_EDITOR) && defined(CATTY_WITH_IMGUI)
		PushOverlay(std::make_unique<Catty::FEditorLayer>());
#endif
		PushOverlay(std::make_unique<Catty::FScriptLayer>(
			GetScriptSystem(),
			GetConfig().ProjectScriptsDir));
		return true;
	}

	virtual void PreShutdown() override
	{
	}
};

Catty::FApp* Catty::CreateApplication()
{
	return new {{APP_CLASS}}();
}
