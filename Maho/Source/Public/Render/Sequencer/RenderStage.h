#pragma once

#include <cstdint>

namespace Maho
{

/**
 * Fixed render pipeline stages (CPU prep → kick RHI).
 * Runs on FRenderServer today (Game thread); later on a dedicated Render worker.
 * Not the same as EEngineStage / FApp TickGroups.
 */
enum class ERenderStage : std::uint8_t
{
	/** Per-frame setup after Game handed off (packet / view extract placeholder). */
	BeginFrame = 0,
	/** Consume / validate frame inputs (future: FramePacket). */
	ProcessPacket,
	Cull,
	BuildDrawLists,
	/** Staging uploads, resource barriers prep, etc. */
	UploadPrep,
	/**
	 * Built-in / extensions that enqueue FRHIServer Submit*.
	 * FRenderServer also runs ImGui EndFrame+capture and default clear submit here if no extension owns it.
	 */
	KickRHI,
	EndFrame,
	COUNT
};

} // namespace Maho
