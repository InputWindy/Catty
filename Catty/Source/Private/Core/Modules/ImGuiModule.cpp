#include "Catty/Core/Modules/ImGuiModule.h"

#include "Catty/Core/App.h"
#include "Catty/Core/Log.h"
#include "Catty/Core/Modules/PlatformModule.h"
#include "Catty/Core/Modules/RenderModule.h"

#include <imgui.h>

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
			CATTY_CORE_ERROR("FImGuiModule: missing Platform/Render");
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
		return true;
	}

	case EModuleStage::BeginFrame:
		ImGui.BeginFrame();
		return true;

	case EModuleStage::ProcessInput:
		if (ImGui.IsInitialized())
		{
			const ImGuiIO& IO = ImGui::GetIO();
			if (!IO.WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_Escape))
			{
				OnExitRequested.Broadcast();
			}
		}
		return true;

	case EModuleStage::PostRender:
		// After Module Render + Layer OnRender: close ImGui frame then present.
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
