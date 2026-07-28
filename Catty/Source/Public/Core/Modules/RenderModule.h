#pragma once

#include <Core/Export.h>
#include <Core/Module.h>
#include <Render/RenderServer.h>
#include <Render/UI/ImGuiSystem.h>

#include <vector>

namespace Catty
{

/** Built-in render server + RHI + Dear ImGui module (always-on in Catty.dll). */
class CATTY_API FRenderModule final : public IModule
{
public:
	const char* GetName() const override { return "Render"; }

	void GetDependencies(std::vector<std::string>& OutNames) const override
	{
		OutNames.push_back("Platform");
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
