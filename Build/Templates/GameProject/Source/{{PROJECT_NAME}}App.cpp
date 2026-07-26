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

	virtual void Tick(float DeltaSeconds) override
	{
		FApp::Tick(DeltaSeconds);

		if (GetEngine().GetFrameIndex() >= 3)
		{
			RequestExit();
		}
	}
};

Catty::FApp* Catty::CreateApplication()
{
	return new {{APP_CLASS}}();
}
