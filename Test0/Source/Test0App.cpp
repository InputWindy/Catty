#include <Catty/Catty.h>
#include <Catty/EntryPoint.h>

class FTest0App : public Catty::FApp
{
protected:
	virtual void Configure(Catty::FEngineConfig& OutConfig) override
	{
		OutConfig.ApplicationName = "Test0";
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

		// Headless smoke: exit after a few frames (windowed loop comes later).
		if (GetEngine().GetFrameIndex() >= 3)
		{
			RequestExit();
		}
	}
};

Catty::FApp* Catty::CreateApplication()
{
	return new FTest0App();
}
