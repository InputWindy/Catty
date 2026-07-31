#pragma once

#include <Core/Export.h>
#include <Core/Sequencer/EngineExtension.h>
#include <Core/Sequencer/EngineStage.h>

namespace Catty
{

class FScript;

/**
 * Programmable Lua layer (Priority Overlay / Layer).
 * VM lifetime is FScript; this layer loads main.lua on Attach and drives OnUpdate / OnFixedUpdate.
 */
class CATTY_API FScriptLayer final : public FLayer
{
public:
	FScriptLayer();

	bool ExecuteStage(EEngineStage Stage) override;

private:
	[[nodiscard]] FScript* TryGetScript() const;
};

} // namespace Catty
