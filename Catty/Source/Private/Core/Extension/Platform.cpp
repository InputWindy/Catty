#include <Core/Extension/Platform.h>

#include <Core/Application/App.h>
#include <Core/System/Console.h>
#include <Core/System/Log.h>

#include <algorithm>

namespace Catty
{

namespace
{

static TAutoConsoleVariable GCVarHeadlessAutoExitFrames(
	"app.Headless.AutoExitFrames",
	3,
	"Headless auto-exit after N frames (when Window.Create=0)");

} // namespace

bool FPlatformSystem::ExecuteStage(EEngineStage Stage)
{
	switch (Stage)
	{
	case EEngineStage::Init:
	{
		if (!GApp)
		{
			CATTY_CORE_ERROR("FPlatformSystem: GApp missing at Init");
			return false;
		}
		const FConfig& Config = GApp->GetConfig();
		FPlatformWindowDesc PlatformDesc;
		PlatformDesc.Platform = Config.Platform;
		PlatformDesc.Title = Config.ApplicationName.empty() ? "Catty" : Config.ApplicationName;
		PlatformDesc.Width = Config.WindowWidth;
		PlatformDesc.Height = Config.WindowHeight;
		PlatformDesc.bResizable = Config.bResizableWindow;
		PlatformDesc.bHeadless = !Config.bCreateMainWindow;

		PlatformWindow = FPlatformWindowFactory::Create(PlatformDesc);
		if (!PlatformWindow)
		{
			CATTY_CORE_ERROR("FPlatformSystem: failed to create platform window");
			return false;
		}

		if (PlatformDesc.bHeadless)
		{
			bAutoExitAfterFrames = true;
			AutoExitFrameCount = static_cast<std::uint64_t>(
				(std::max)(1, GCVarHeadlessAutoExitFrames.GetValue()));
			CATTY_CORE_INFO("Platform window headless; auto-exit after {} frames", AutoExitFrameCount);
		}

		AppRequestExitHandle = OnRequestExit.AddRaw(GApp, &FApp::OnRequestExit);
		return true;
	}
	case EEngineStage::BeginFrame:
	{
		bool bShouldRequestExit = false;

		if (PlatformWindow)
		{
			PlatformWindow->PollEvents();
			if (PlatformWindow->ShouldClose())
			{
				bShouldRequestExit = true;
			}
		}

		if (bAutoExitAfterFrames && GApp && GApp->GetFrameIndex() >= AutoExitFrameCount)
		{
			bShouldRequestExit = true;
		}

		if (bShouldRequestExit)
		{
			OnRequestExit.Broadcast();
		}
		return true;
	}
	case EEngineStage::Shutdown:
		if (AppRequestExitHandle.IsValid())
		{
			OnRequestExit.Remove(AppRequestExitHandle);
			AppRequestExitHandle.Reset();
		}
		OnRequestExit.Clear();
		PlatformWindow.reset();
		FPlatformWindowFactory::Shutdown();
		return true;
	default:
		return true;
	}
}

} // namespace Catty
