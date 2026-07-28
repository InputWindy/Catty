#pragma once

#include "Catty/Core/Export.h"
#include "Catty/Core/Module.h"
#include "Catty/UI/ImGuiSystem.h"

#include <vector>

namespace Catty
{

/** Dear ImGui. Depends on Render + Platform. */
class CATTY_API FImGuiModule final : public IModule
{
public:
	const char* GetName() const override { return "ImGui"; }

	void GetDependencies(std::vector<std::string>& OutNames) const override
	{
		OutNames.push_back("Render");
		OutNames.push_back("Platform");
	}

	bool OnStage(EModuleStage Stage, FApp& App, FStageContext& Ctx) override;

	[[nodiscard]] FImGuiSystem& GetImGui() { return ImGui; }
	[[nodiscard]] const FImGuiSystem& GetImGui() const { return ImGui; }

private:
	FImGuiSystem ImGui;
};

} // namespace Catty
