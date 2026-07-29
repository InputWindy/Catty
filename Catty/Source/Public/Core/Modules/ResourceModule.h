#pragma once

#include <Core/Export.h>
#include <Core/Module.h>
#include <Core/Resource/ResourceManager.h>

#include <vector>

namespace Catty
{

/** Built-in resource manager module (owns FResourceManager + private FResourceServer). */
class CATTY_API FResourceModule final : public IModule
{
public:
	const char* GetName() const override { return "Resource"; }

	void GetDependencies(EModuleStage Stage, std::vector<std::string>& OutNames) const override
	{
		switch (Stage)
		{
		case EModuleStage::PreInit:
		case EModuleStage::Init:
		case EModuleStage::PostInit:
			OutNames.push_back("GC");
			break;
		default:
			break;
		}
	}

	bool ExecuteStage(EModuleStage Stage, FApp& App, FStageContext& Ctx) override;
	[[nodiscard]] bool IsIdle() const override;

	[[nodiscard]] FResourceManager& GetResourceManager() { return ResourceManager; }
	[[nodiscard]] const FResourceManager& GetResourceManager() const { return ResourceManager; }

private:
	FResourceManager ResourceManager;
};

} // namespace Catty
