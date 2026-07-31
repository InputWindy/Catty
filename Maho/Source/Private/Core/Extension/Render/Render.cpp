#include <Core/Extension/Render/Render.h>

#include <Core/Application/App.h>
#include <Core/System/Log.h>

namespace Maho
{

bool FRenderSystem::ExecuteStage(EEngineStage Stage)
{
	switch (Stage)
	{
	case EEngineStage::Init:
	{
		if (!GApp)
		{
			MAHO_CORE_ERROR("FRenderSystem: GApp missing at Init");
			return false;
		}
		FPlatformSystem* Platform = GApp->GetExtension<FPlatformSystem>();
		FPlatformWindow* Window = Platform ? Platform->GetWindow() : nullptr;
		if (!Window)
		{
			MAHO_CORE_ERROR("FRenderSystem: no platform window at Init");
			return false;
		}
		if (!RenderServer.Boot(*Window, GApp->GetConfig()))
		{
			MAHO_CORE_ERROR("FRenderSystem: RenderServer.Boot failed");
			return false;
		}
		return true;
	}
	case EEngineStage::BeginFrame:
		if (GApp && RenderServer.GetImGui().IsInitialized())
		{
			RenderServer.WaitBeforeImGuiNewFrame(GApp->GetFrameIndex());
			RenderServer.GetImGui().BeginFrame();
		}
		return true;
	case EEngineStage::Render:
		if (GApp)
		{
			RenderServer.Render(GApp->GetFrameIndex());
		}
		return true;
	case EEngineStage::Shutdown:
		RenderServer.TearDown();
		return true;
	default:
		return true;
	}
}

} // namespace Maho
