#include "Catty/Core/Modules/RenderModule.h"

#include "Catty/Core/App.h"
#include "Catty/Core/Log.h"
#include "Catty/Core/Modules/ImGuiModule.h"
#include "Catty/Core/Modules/PlatformModule.h"

namespace Catty
{

void FRenderModule::Flush()
{
	if (RenderServer.IsInitialized())
	{
		RenderServer.Flush();
	}
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

bool FRenderModule::OnStage(EModuleStage Stage, FApp& App, FStageContext& Ctx)
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
		}
		return true;
	}

	case EModuleStage::BeginFrame:
		SyncFramebufferSize(App);
		return true;

	case EModuleStage::PostRender:
	{
		// When ImGui is active it owns EndFrame + clear/present (after Layer OnRender).
		FImGuiModule* ImGuiMod = App.GetModule<FImGuiModule>();
		if (ImGuiMod && ImGuiMod->GetImGui().IsInitialized())
		{
			return true;
		}

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
		return true;
	}

	case EModuleStage::PreShutdown:
		Flush();
		return true;

	case EModuleStage::Shutdown:
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
