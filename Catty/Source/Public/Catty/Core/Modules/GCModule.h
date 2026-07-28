#pragma once

#include "Catty/Core/Export.h"
#include "Catty/Core/Module.h"
#include "Catty/Resource/GCManager.h"

#include <vector>

namespace Catty
{

class CATTY_API FGCModule final : public IModule
{
public:
	const char* GetName() const override { return "GC"; }

	void GetDependencies(std::vector<std::string>& OutNames) const override
	{
		// After window / render / imgui (legacy InitializeEngine order).
		OutNames.push_back("ImGui");
	}

	bool OnStage(EModuleStage Stage, FApp& App, FStageContext& Ctx) override;

	[[nodiscard]] FGCManager& GetGCManager() { return GCManager; }
	[[nodiscard]] const FGCManager& GetGCManager() const { return GCManager; }

private:
	FGCManager GCManager;
};

} // namespace Catty
