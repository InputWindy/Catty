#pragma once

#include <Core/FrameStage.h>
#include <Core/Layer.h>
#include <Core/Module.h>
#include <Core/Sequencer.h>

#include <cstddef>

namespace Catty
{

struct FFrameCommandPayload
{
};

struct FModuleFrameTraits
{
	using FStage = EFrameStage;
	using FObject = IModule;
	using FPayload = FFrameCommandPayload;

	static constexpr std::size_t NumStages = static_cast<std::size_t>(EFrameStage::COUNT);

	static constexpr std::size_t StageToIndex(FStage Stage)
	{
		return static_cast<std::size_t>(Stage);
	}

	static constexpr FStage IndexToStage(std::size_t Index)
	{
		return static_cast<FStage>(Index);
	}

	static constexpr EStageRepeatPolicy GetStageRepeatPolicy(FStage Stage)
	{
		return Stage == FStage::FixedUpdate
			? EStageRepeatPolicy::AccumulatedFixed
			: EStageRepeatPolicy::Once;
	}

	static constexpr bool ShouldFlushPendingAdd(FStage Stage)
	{
		return Stage == FStage::Attach;
	}

	static constexpr bool ShouldFlushPendingRemove(FStage Stage)
	{
		return Stage == FStage::Detach;
	}
};

struct FLayerFrameTraits
{
	using FStage = EFrameStage;
	using FObject = FLayer;
	using FPayload = FFrameCommandPayload;

	static constexpr std::size_t NumStages = static_cast<std::size_t>(EFrameStage::COUNT);

	static constexpr std::size_t StageToIndex(FStage Stage)
	{
		return static_cast<std::size_t>(Stage);
	}

	static constexpr FStage IndexToStage(std::size_t Index)
	{
		return static_cast<FStage>(Index);
	}

	static constexpr EStageRepeatPolicy GetStageRepeatPolicy(FStage Stage)
	{
		return Stage == FStage::FixedUpdate
			? EStageRepeatPolicy::AccumulatedFixed
			: EStageRepeatPolicy::Once;
	}

	static constexpr bool ShouldFlushPendingAdd(FStage Stage)
	{
		return Stage == FStage::Attach;
	}

	static constexpr bool ShouldFlushPendingRemove(FStage Stage)
	{
		return Stage == FStage::Detach;
	}
};

} // namespace Catty
