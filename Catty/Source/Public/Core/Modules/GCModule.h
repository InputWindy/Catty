#pragma once

#include <Core/Export.h>
#include <Core/Module.h>
#include <Core/GC.h>

#include <vector>

namespace Catty
{

/** Built-in GC module (always-on in Catty.dll). */
class CATTY_API FGCModule final : public IModule
{
public:
	const char* GetName() const override { return "GC"; }

	void GetDependencies(std::vector<std::string>& OutNames) const override
	{
		OutNames.push_back("Render");
	}

	bool ExecuteStage(EModuleStage Stage, FApp& App, FStageContext& Ctx) override;

	[[nodiscard]] FGC& GetGC() { return GC; }
	[[nodiscard]] const FGC& GetGC() const { return GC; }

private:
	FGC GC;
};

} // namespace Catty
