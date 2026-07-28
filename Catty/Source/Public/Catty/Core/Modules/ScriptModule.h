#pragma once

#include "Catty/Core/Delegate.h"
#include "Catty/Core/Export.h"
#include "Catty/Core/Module.h"
#include "Catty/Script/ScriptSystem.h"

#include <vector>

namespace Catty
{

/** Lua script system. Boot after Worker; ILuaBindable types auto-bind via OnLuaReady. */
class CATTY_API FScriptModule final : public IModule
{
public:
	const char* GetName() const override { return "Script"; }

	void GetDependencies(std::vector<std::string>& OutNames) const override
	{
		OutNames.push_back("Worker");
	}

	bool OnStage(EModuleStage Stage, FApp& App, FStageContext& Ctx) override;

	/** Bind/rebind Post hooks after layers so script runs after Layer OnUpdate. */
	void BindPostStageHooks(FApp& App);

	[[nodiscard]] FScriptSystem& GetScriptSystem() { return ScriptSystem; }
	[[nodiscard]] const FScriptSystem& GetScriptSystem() const { return ScriptSystem; }

private:
	FScriptSystem ScriptSystem;
	FDelegateHandle UpdatePostHandle;
	FDelegateHandle FixedUpdatePostHandle;
};

} // namespace Catty
