#pragma once

#include <Core/Export.h>
#include <Core/Sequencer/EngineExtension.h>
#include <Core/Sequencer/EngineStage.h>

namespace Maho
{

class FScriptSystem;

/**
 * Programmable Lua layer (Priority Overlay / Layer).
 * VM lifetime is FScriptSystem; this layer loads main.lua on Attach and drives OnUpdate / OnFixedUpdate.
 */
class MAHO_API FScriptLayer final : public FLayer
{
public:
	FScriptLayer();

	bool ExecuteStage(EEngineStage Stage) override;

private:
	[[nodiscard]] FScriptSystem* TryGetScript() const;
};

} // namespace Maho
