#pragma once

#include "Core/Module.h"
#include "CImGuiSystemApi.h"
#include "UI/ImGuiSystem.h"

#include <vector>

namespace Catty
{

/** Dear ImGui. Plugin id: CImGuiSystem. */
class CATTY_CIMGUISYSTEM_MODULE_API FImGuiModule final : public IModule
{
public:
	const char* GetName() const override { return "CImGuiSystem"; }

	void GetDependencies(std::vector<std::string>& OutNames) const override
	{
		OutNames.push_back("CRenderServer");
		OutNames.push_back("CPlatformWindow");
	}

	bool OnStage(EModuleStage Stage, FApp& App, FStageContext& Ctx) override;

	[[nodiscard]] FImGuiSystem& GetImGui() { return ImGui; }
	[[nodiscard]] const FImGuiSystem& GetImGui() const { return ImGui; }

private:
	FImGuiSystem ImGui;
};

} // namespace Catty
