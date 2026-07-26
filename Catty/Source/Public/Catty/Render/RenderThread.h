#pragma once

#include "Catty/Core/Export.h"

#include <cstdint>
#include <functional>
#include <memory>

namespace Catty
{

using FRenderTask = std::function<void()>;

/**
 * Dedicated render-server thread.
 * Game thread enqueues work and continues; call Flush() at the PreRender sync point
 * to wait until all previously submitted tasks have finished.
 */
class CATTY_API FRenderThread
{
public:
	FRenderThread();
	~FRenderThread();

	FRenderThread(const FRenderThread&) = delete;
	FRenderThread& operator=(const FRenderThread&) = delete;

	bool Start(const char* ThreadName = "CattyRenderThread");
	void Stop();

	[[nodiscard]] bool IsRunning() const;

	/** Enqueue a task for the render thread (non-blocking). */
	void Enqueue(FRenderTask Task);

	/**
	 * Block the calling thread until every task submitted before this call has run.
	 * Used by FApp at the PreRender sync point.
	 */
	void Flush();

private:
	struct FImpl;
	std::unique_ptr<FImpl> Impl;
};

} // namespace Catty
