#include <Catty/Catty.h>
#include <Catty/EntryPoint.h>
#include <Catty/Script/ScriptLayer.h>

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

	virtual void RegisterModules() override
	{
		RegisterModule(std::make_unique<Catty::FEngineModule>());
		RegisterModule(std::make_unique<Catty::FPlatformModule>());
		RegisterModule(std::make_unique<Catty::FRenderModule>());
		RegisterModule(std::make_unique<Catty::FImGuiModule>());
		RegisterModule(std::make_unique<Catty::FGCModule>());
		RegisterModule(std::make_unique<Catty::FResourceModule>());
		RegisterModule(std::make_unique<Catty::FWorkerModule>());
	}

	virtual bool PostInitialize() override
	{
		PushLayer(std::make_unique<FWorldLayer>("MainWorld"));

#if defined(GAME_WITH_EDITOR)
		PushOverlay(std::make_unique<FEditorLayer>());
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
