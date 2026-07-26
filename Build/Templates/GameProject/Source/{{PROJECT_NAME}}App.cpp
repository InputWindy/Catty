#include <Catty/Catty.h>
#include <Catty/EntryPoint.h>

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
	}

	virtual bool PostInitialize() override
	{
		// Engine is ready: create window / register subsystems / load entry map here later.
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
