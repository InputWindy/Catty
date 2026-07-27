#include <Catty/Catty.h>
#include <Catty/EntryPoint.h>

#include "Editor/EditorLayer.h"
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

#if defined(GAME_WITH_EDITOR)
		PushOverlay(std::make_unique<FEditorLayer>());
#endif
		return true;
	}

	virtual void PreShutdown() override
	{
		// Tear down game-side resources before the base Shutdown tears down the engine.
	}
};

Catty::FApp* Catty::CreateApplication()
{
	return new {{APP_CLASS}}();
}
