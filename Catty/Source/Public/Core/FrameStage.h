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

[[nodiscard]] inline const char* FrameStageName(EFrameStage Stage)
{
	switch (Stage)
	{
	case EFrameStage::Attach: return "Attach";
	case EFrameStage::BeginFrame: return "BeginFrame";
	case EFrameStage::ProcessInput: return "ProcessInput";
	case EFrameStage::FixedUpdate: return "FixedUpdate";
	case EFrameStage::Update: return "Update";
	case EFrameStage::LateUpdate: return "LateUpdate";
	case EFrameStage::PreRender: return "PreRender";
	case EFrameStage::Render: return "Render";
	case EFrameStage::PostRender: return "PostRender";
	case EFrameStage::EndFrame: return "EndFrame";
	case EFrameStage::Detach: return "Detach";
	case EFrameStage::PrepareExit: return "PrepareExit";
	default: return "?";
	}
}

} // namespace Catty
