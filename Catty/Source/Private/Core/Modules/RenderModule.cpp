#include <Core/Modules/RenderModule.h>

#include <Core/App.h>
#include <Core/Log.h>
#include <Core/Modules/PlatformModule.h>

namespace Catty
{

void FRenderModule::Flush()
{
	if (RenderServer.IsInitialized())
	{
		RenderServer.Flush();
	}
}

void FRenderModule::ClearPresentAndFlush(FApp& App)
{
	if (RenderServer.HasRHI())
	{
		const FEngineConfig& Config = App.GetConfig();
		RenderServer.RequestClearPresent(
			Config.ClearColorR,
			Config.ClearColorG,
			Config.ClearColorB,
			Config.ClearColorA);
	}
	Flush();
}

void FRenderModule::SyncFramebufferSize(FApp& App)
{
	FPlatformModule* Platform = App.GetModule<FPlatformModule>();
	if (!Platform)
	{
		return;
	}

	FPlatformWindow* Window = Platform->GetWindow();
	if (!Window || !Window->HasOsWindow() || !RenderServer.HasRHI())
	{
		return;
	}

	int Width = 0;
	int Height = 0;
	Window->GetFramebufferSize(Width, Height);
	if (Width <= 0 || Height <= 0)
	{
		return;
	}

	if (Width != LastFramebufferWidth || Height != LastFramebufferHeight)
	{
		LastFramebufferWidth = Width;
		LastFramebufferHeight = Height;
		RenderServer.RequestResize(Width, Height);
	}
}

bool FRenderModule::ExecuteStage(EModuleStage Stage, FApp& App, FStageContext& Ctx)
{
	(void)Ctx;
	switch (Stage)
	{
	case EModuleStage::Init:
	{
		FPlatformModule* Platform = App.GetModule<FPlatformModule>();
		if (!Platform || !Platform->GetWindow())
		{
			CATTY_CORE_ERROR("FRenderModule: Platform window missing");
			return false;
		}

		FPlatformWindow& Window = *Platform->GetWindow();
		if (!RenderServer.Initialize())
		{
			CATTY_CORE_ERROR("FRenderModule: RenderServer Initialize failed");
			return false;
		}

		if (!RenderServer.InitializeRHI(Window))
		{
			CATTY_CORE_ERROR("FRenderModule: RHI Initialize failed");
			RenderServer.Shutdown();
			return false;
		}

		if (Window.HasOsWindow())
		{
			Window.GetFramebufferSize(LastFramebufferWidth, LastFramebufferHeight);
			if (!ImGui.Initialize(Window, RenderServer, App.GetConfig().ProjectConfigDir))
			{
				CATTY_CORE_ERROR("FRenderModule: ImGui Initialize failed");
				RenderServer.Shutdown();
				return false;
			}
		}
		return true;
	}
	case EModuleStage::BeginFrame:
		SyncFramebufferSize(App);
		ImGui.BeginFrame();
		return true;
	case EModuleStage::ProcessInput:
		if (ImGui.IsInitialized() && ImGui.PollExitRequest())
		{
			OnExitRequested.Broadcast();
		}
		return true;
	case EModuleStage::PostRender:
		if (ImGui.IsInitialized())
		{
			ImGui.EndFrame();
		}
		ClearPresentAndFlush(App);
		return true;
	case EModuleStage::PreShutdown:
		Flush();
		return true;
	case EModuleStage::Shutdown:
		if (ImGui.IsInitialized())
		{
			ImGui.Shutdown(RenderServer);
		}
		if (RenderServer.IsInitialized())
		{
			RenderServer.Shutdown();
		}
		return true;
	default:
		return true;
	}
}

} // namespace Catty
