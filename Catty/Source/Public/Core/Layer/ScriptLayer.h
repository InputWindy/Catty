#pragma once

#include <Core/Export.h>
#include <Core/Layer.h>
#include <Core/Layer/ScriptSystem.h>

#include <string>

namespace Catty
{

/**
 * Programmable Lua layer (not an engine Module).
 * Owns no ScriptSystem — uses FApp::GetScriptSystem(); Init on Attach.
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
