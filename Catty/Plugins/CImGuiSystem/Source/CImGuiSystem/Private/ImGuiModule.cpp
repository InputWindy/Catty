#include "ImGuiModule.h"

#include "Core/App.h"
#include "Core/Log.h"
#include "PlatformModule.h"
#include "RenderModule.h"

namespace Catty
{

bool FImGuiModule::OnStage(EModuleStage Stage, FApp& App, FStageContext& Ctx)
{
	(void)Ctx;
	switch (Stage)
	{
	case EModuleStage::Init:
	{
		FPlatformModule* Platform = App.GetModule<FPlatformModule>();
		FRenderModule* Render = App.GetModule<FRenderModule>();
		if (!Platform || !Platform->GetWindow() || !Render)
		{
			CATTY_CORE_ERROR("FImGuiModule: missing CPlatformWindow/CRenderServer");
			return false;
		}

		FPlatformWindow& Window = *Platform->GetWindow();
		if (!Window.HasOsWindow())
		{
			return true;
		}

		if (!ImGui.Initialize(Window, Render->GetServer(), App.GetConfig().ProjectConfigDir))
		{
			CATTY_CORE_ERROR("FImGuiModule: Initialize failed");
			return false;
		}
		App.SetPresentOwnedExternally(true);
		return true;
	}
	case EModuleStage::BeginFrame:
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
			if (FRenderModule* Render = App.GetModule<FRenderModule>())
			{
				FRenderServer& Server = Render->GetServer();
				if (Server.HasRHI())
				{
					const FEngineConfig& Config = App.GetConfig();
					Server.RequestClearPresent(
						Config.ClearColorR,
						Config.ClearColorG,
						Config.ClearColorB,
						Config.ClearColorA);
				}
				if (Server.IsInitialized())
				{
					Server.Flush();
				}
			}
		}
		return true;
	case EModuleStage::Shutdown:
	{
		App.SetPresentOwnedExternally(false);
		if (ImGui.IsInitialized())
		{
			if (FRenderModule* Render = App.GetModule<FRenderModule>())
			{
				ImGui.Shutdown(Render->GetServer());
			}
		}
		return true;
	}
	default:
		return true;
	}
}

} // namespace Catty
