#pragma once

#include <Core/Export.h>
#include <Core/Module.h>
#include <Render/RenderServer.h>
#include <Render/UI/ImGuiSystem.h>

#include <vector>

namespace Catty
{

/** Built-in render server + RHI + Dear ImGui module (always-on in Catty.dll). */
class CATTY_API FRender final : public IModule
{
public:
	const char* GetName() const override { return "Render"; }

	void GetDependencies(EModuleStage Stage, std::vector<std::string>& OutNames) const override
	{
		switch (Stage)
		{
		case EModuleStage::PreInit:
		case EModuleStage::Init:
		case EModuleStage::PostInit:
		case EModuleStage::BeginFrame:
		case EModuleStage::ProcessInput:
		case EModuleStage::FixedUpdate:
		case EModuleStage::Update:
		case EModuleStage::LateUpdate:
		case EModuleStage::PreRender:
		case EModuleStage::Render:
		case EModuleStage::PostRender:
		case EModuleStage::EndFrame:
		case EModuleStage::PrepareExit:
		case EModuleStage::Shutdown:
			OutNames.push_back("Platform");
			break;
		default:
			break;
		}
	}

	bool ExecuteStage(EModuleStage Stage, FApp& App, FStageContext& Ctx) override;

	[[nodiscard]] FRenderServer& GetServer() { return RenderServer; }
	[[nodiscard]] const FRenderServer& GetServer() const { return RenderServer; }

	[[nodiscard]] FImGuiSystem& GetImGui() { return ImGui; }
	[[nodiscard]] const FImGuiSystem& GetImGui() const { return ImGui; }

private:
	void SyncFramebufferSize(FApp& App);
	void Flush();
	void ClearPresentAndFlush(FApp& App);

	FRenderServer RenderServer;
	FImGuiSystem ImGui;
	int LastFramebufferWidth = 0;
	int LastFramebufferHeight = 0;
};

} // namespace Catty
