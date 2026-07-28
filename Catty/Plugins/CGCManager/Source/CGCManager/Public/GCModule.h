#pragma once

#include "Core/Module.h"
#include "CGCManagerApi.h"
#include "Resource/GCManager.h"

#include <vector>

namespace Catty
{

/** GC manager module. Plugin id: CGCManager. */
class CATTY_CGCMANAGER_MODULE_API FGCModule final : public IModule
{
public:
	const char* GetName() const override { return "CGCManager"; }

	void GetDependencies(std::vector<std::string>& OutNames) const override
	{
		OutNames.push_back("CImGuiSystem");
	}

	bool OnStage(EModuleStage Stage, FApp& App, FStageContext& Ctx) override;

	[[nodiscard]] FGCManager& GetGCManager() { return GCManager; }
	[[nodiscard]] const FGCManager& GetGCManager() const { return GCManager; }

private:
	FGCManager GCManager;
};

} // namespace Catty
