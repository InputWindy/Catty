#pragma once

#include <Core/Export.h>

#include <cstddef>
#include <functional>
#include <memory>

namespace Maho
{

/**
 * Persistent worker pool for data-parallel split work (and optional fire-and-forget Push).
 *
 * Primary pattern: Fork(N, Body) runs Body(WorkerIndex) for WorkerIndex in [0, N),
 * then joins. Capture your context in the lambda; use WorkerIndex to select a segment.
 *
 * Example (vector ParallelFor-style at the call site):
 * ```
 *   struct FRangeContext { const T* Data; int* Out; std::size_t Count; };
 *   FRangeContext Ctx{...};
 *   const std::size_t Threads = 4;
 *   GApp->GetExtension<FWorkerPoolSystem>()->GetPool().Fork(Threads, [&Ctx, Threads](std::size_t WorkerIndex)
 *   {
 *       const std::size_t Begin = Ctx.Count * WorkerIndex / Threads;
 *       const std::size_t End = Ctx.Count * (WorkerIndex + 1) / Threads;
 *       for (std::size_t I = Begin; I < End; ++I) { ... }
 *   });
 * ```
 *
 * Push/Flush remain for unrelated background jobs. ParallelFor is not a pool API —
 * build it on Fork at the use site.
 */
class MAHO_API FWorkerPool
{
public:
	FWorkerPool();
	~FWorkerPool();

	FWorkerPool(const FWorkerPool&) = delete;
	FWorkerPool& operator=(const FWorkerPool&) = delete;

	/**
	 * Start worker threads.
	 * @param NumWorkers 0 = use CVar worker.NumThreads (0 there = auto from hardware).
	 */
	[[nodiscard]] bool Initialize(std::size_t NumWorkers = 0);
	void Shutdown();

	[[nodiscard]] bool IsInitialized() const { return bInitialized; }
	[[nodiscard]] std::size_t GetNumWorkers() const;
	/** True when no Push'd tasks are outstanding. */
	[[nodiscard]] bool IsIdle() const;

	/**
	 * Run Body on NumWorkers logical workers with indices [0, NumWorkers), then join.
	 * Calling thread executes WorkerIndex 0; the rest are Push'd to the pool
	 * (avoids deadlock if the pool is busy). If the pool is down or NumWorkers<=1,
	 * runs serially on the caller.
	 *
	 * Context: capture by lambda. WorkerIndex selects the segment inside that context.
	 */
	void Fork(std::size_t NumWorkers, const std::function<void(std::size_t WorkerIndex)>& Body);

	/** Non-blocking submit of an unrelated job. No-op if not initialized or Task is empty. */
	void Push(std::function<void()> Task);

	/** Block until every Push'd task submitted before this call has finished. */
	void Flush();

private:
	struct FImpl;
	std::unique_ptr<FImpl> Impl;
	bool bInitialized = false;
};

} // namespace Maho
