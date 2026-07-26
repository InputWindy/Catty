#pragma once

#include "Catty/Core/Export.h"
#include "Catty/Render/RenderThread.h"

namespace Catty
{

/**
 * Render server facade (Vulkan-backed later).
 * Owns the dedicated render thread; game/engine code submits work through this.
 */
class CATTY_API FRenderServer
{
public:
	FRenderServer();
	~FRenderServer();

	FRenderServer(const FRenderServer&) = delete;
	FRenderServer& operator=(const FRenderServer&) = delete;

	/** Start render thread and verify Vulkan loader/headers are available. */
	bool Initialize();

	/** Flush pending work, then stop the render thread. */
	void Shutdown();

	[[nodiscard]] bool IsInitialized() const { return bInitialized; }

	[[nodiscard]] FRenderThread& GetRenderThread() { return RenderThread; }
	[[nodiscard]] const FRenderThread& GetRenderThread() const { return RenderThread; }

	void Enqueue(FRenderTask Task);
	void Flush();

private:
	bool ProbeVulkan() const;

	FRenderThread RenderThread;
	bool bInitialized = false;
};

} // namespace Catty
