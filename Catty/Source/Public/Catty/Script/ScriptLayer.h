#pragma once

#include "Catty/Core/Export.h"
#include "Catty/Core/Layer.h"
#include "Catty/Script/ScriptSystem.h"

#include <string>

namespace Catty
{

/**
 * Programmable Lua layer (not an engine Module).
 * Owns no ScriptSystem — uses FApp's FScriptSystem; Init on Attach, tick via OnUpdate / OnFixedUpdate.
 * Games PushOverlay this after World / Editor so Lua runs last among content layers.
 */
class CATTY_API FScriptLayer final : public FLayer
{
public:
	FScriptLayer(FScriptSystem& Script, std::string ScriptsDirectory);

	void OnAttach() override;
	void OnDetach() override;

	void OnUpdate(EModuleStage Stage, FApp& App, FStageContext& Ctx) override;
	void OnFixedUpdate(EModuleStage Stage, FApp& App, FStageContext& Ctx) override;

	[[nodiscard]] FScriptSystem& GetScriptSystem() { return Script; }
	[[nodiscard]] const FScriptSystem& GetScriptSystem() const { return Script; }

private:
	FScriptSystem& Script;
	std::string ScriptsDirectory;
};

} // namespace Catty
