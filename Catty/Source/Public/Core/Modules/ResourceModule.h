#pragma once

#include <Core/Delegate.h>
#include <Core/Export.h>
#include <Core/Module.h>
#include <Core/Resource/ResourceManager.h>

#include <vector>

namespace Catty
{

class FScriptSystem;

/** Built-in resource manager module (owns FResourceManager + private FResourceServer). */
class CATTY_API FResourceModule final : public IModule
{
public:
	const char* GetName() const override { return "Resource"; }

	void GetDependencies(std::vector<std::string>& OutNames) const override
	{
		OutNames.push_back("GC");
	}

	bool ExecuteStage(EModuleStage Stage, FApp& App, FStageContext& Ctx) override;

	[[nodiscard]] FResourceManager& GetResourceManager() { return ResourceManager; }
	[[nodiscard]] const FResourceManager& GetResourceManager() const { return ResourceManager; }

private:
	void OnLuaReady(FScriptSystem& Script);

	FResourceManager ResourceManager;
	FDelegateHandle LuaReadyHandle;
};

} // namespace Catty
