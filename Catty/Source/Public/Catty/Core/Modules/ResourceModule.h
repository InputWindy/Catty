#pragma once

#include "Catty/Core/Delegate.h"
#include "Catty/Core/Export.h"
#include "Catty/Core/Module.h"
#include "Catty/Resource/ResourceManager.h"

#include <vector>

namespace Catty
{

class FScriptSystem;

class CATTY_API FResourceModule final : public IModule
{
public:
	const char* GetName() const override { return "Resource"; }

	void GetDependencies(std::vector<std::string>& OutNames) const override
	{
		OutNames.push_back("GC");
	}

	bool OnStage(EModuleStage Stage, FApp& App, FStageContext& Ctx) override;

	[[nodiscard]] FResourceManager& GetResourceManager() { return ResourceManager; }
	[[nodiscard]] const FResourceManager& GetResourceManager() const { return ResourceManager; }

private:
	void OnLuaReady(FScriptSystem& Script);

	FResourceManager ResourceManager;
	FDelegateHandle LuaReadyHandle;
};

} // namespace Catty
