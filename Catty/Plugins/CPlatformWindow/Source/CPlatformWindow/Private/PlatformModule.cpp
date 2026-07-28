#include "PlatformModule.h"

#include "Core/App.h"
#include "Core/ConsoleManager.h"
#include "Core/Log.h"

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

bool FPlatformModule::OnStage(EModuleStage Stage, FApp& App, FStageContext& Ctx)
{
	(void)Ctx;
	switch (Stage)
	{
	case EModuleStage::Init:
	{
		const FEngineConfig& Config = App.GetConfig();
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
			CATTY_CORE_ERROR("FPlatformModule: failed to create platform window");
			return false;
		}

		if (PlatformDesc.bHeadless)
		{
			bAutoExitAfterFrames = true;
			AutoExitFrameCount = static_cast<std::uint64_t>(
				(std::max)(1, GCVarHeadlessAutoExitFrames.GetValue()));
			CATTY_CORE_INFO("Platform window headless; auto-exit after {} frames", AutoExitFrameCount);
		}
		return true;
	}
	case EModuleStage::BeginFrame:
		if (PlatformWindow)
		{
			PlatformWindow->PollEvents();
			if (PlatformWindow->ShouldClose())
			{
				OnExitRequested.Broadcast();
			}
		}
		return true;
	case EModuleStage::EndFrame:
		if (bAutoExitAfterFrames && App.GetFrameIndex() >= AutoExitFrameCount)
		{
			CATTY_CORE_INFO(
				"Requesting exit after {} frames (headless auto-exit)",
				AutoExitFrameCount);
			OnExitRequested.Broadcast();
		}
		return true;
	case EModuleStage::Shutdown:
		PlatformWindow.reset();
		FPlatformWindowFactory::Shutdown();
		return true;
	default:
		return true;
	}
}

} // namespace Catty
