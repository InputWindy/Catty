#pragma once

#include <SampleApi.h>

#include <Core/Sequencer/EngineExtension.h>
#include <Core/Sequencer/EngineStage.h>

namespace Maho
{

/**
 * Minimal optional plugin extension.
 * Lives in Sample.dll (not Maho.dll). Register from the game App when the plugin is enabled.
 */
class MAHO_SAMPLE_API FSampleModule final : public IEngineExtension
{
public:
	[[nodiscard]] const char* GetName() const override { return "Sample"; }

	bool ExecuteStage(EEngineStage Stage) override;
};

} // namespace Maho
