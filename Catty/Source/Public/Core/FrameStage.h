#pragma once

#include <cstddef>
#include <cstdint>

namespace Catty
{

/** Per-frame stages for SequenceGraph Module/Layer sequencers (Init/Shutdown stay on EModuleStage). */
enum class EFrameStage : std::uint8_t
{
	Attach = 0,
	BeginFrame,
	ProcessInput,
	FixedUpdate,
	Update,
	LateUpdate,
	PreRender,
	Render,
	PostRender,
	EndFrame,
	Detach,
	PrepareExit,
	COUNT
};

enum class EStageRepeatPolicy : std::uint8_t
{
	Once = 0,
	AccumulatedFixed
};

} // namespace Catty
