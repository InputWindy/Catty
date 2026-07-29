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

	void GetDependencies(EModuleStage Stage, std::vector<std::string>& OutNames) const override
	{
		switch (Stage)
		{
		case EModuleStage::PreInit:
		case EModuleStage::Init:
		case EModuleStage::PostInit:
			OutNames.push_back("Render");
			break;
		case EModuleStage::PrepareExit:
		case EModuleStage::Shutdown:
			// Wait for Resource to drop catalog refs before GC purge / pool tear-down.
			OutNames.push_back("Resource");
			break;
		default:
			break;
		}
	}

	bool ExecuteStage(EModuleStage Stage, FApp& App, FStageContext& Ctx) override;
	[[nodiscard]] bool IsIdle() const override;

	[[nodiscard]] FGC& GetGC() { return GC; }
	[[nodiscard]] const FGC& GetGC() const { return GC; }

private:
	FGC GC;
};

} // namespace Catty
