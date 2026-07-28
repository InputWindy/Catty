#pragma once

#include <Core/Export.h>
#include <Core/Module.h>
#include <Core/GCManager.h>

#include <vector>

namespace Catty
{

/** Built-in GC manager module (always-on in Catty.dll). */
class CATTY_API FGCModule final : public IModule
{
public:
	const char* GetName() const override { return "GC"; }

	void GetDependencies(std::vector<std::string>& OutNames) const override
	{
		OutNames.push_back("Render");
	}

	bool ExecuteStage(EModuleStage Stage, FApp& App, FStageContext& Ctx) override;

	[[nodiscard]] FGCManager& GetGCManager() { return GCManager; }
	[[nodiscard]] const FGCManager& GetGCManager() const { return GCManager; }

private:
	FGCManager GCManager;
};

} // namespace Catty
