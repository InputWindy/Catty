#include <Core/WorkerPool.h>

#include <Core/ConsoleManager.h>
#include <Core/Log.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace Catty
{

namespace
{

static TAutoConsoleVariable GCVarNumWorkers(
	"worker.NumThreads",
	0,
	"FWorkerPool size for Fork/Push (0 = auto: clamp hardware_concurrency to [1,4])");

[[nodiscard]] std::size_t ResolveWorkerCount(std::size_t Requested)
{
	if (Requested > 0)
	{
		return Requested;
	}

	const int FromCVar = GCVarNumWorkers.GetValue();
	if (FromCVar > 0)
	{
		return static_cast<std::size_t>(FromCVar);
	}

	const unsigned Hardware = std::thread::hardware_concurrency();
	const unsigned AutoCount = (Hardware == 0) ? 2u : Hardware;
	return static_cast<std::size_t>((std::max)(1u, (std::min)(4u, AutoCount)));
}

} // namespace

struct FWorkerPool::FImpl
{
	std::vector<std::thread> Workers;
	mutable std::mutex Mutex;
	std::condition_variable WorkCv;
	std::condition_variable IdleCv;
	std::queue<std::function<void()>> Queue;
	bool bStopRequested = false;
	std::uint32_t PendingCount = 0;

	void WorkerMain(std::size_t WorkerIndex)
	{
		CATTY_CORE_INFO("WorkerPool thread {} online", WorkerIndex);

		for (;;)
		{
			std::function<void()> Task;
			{
				std::unique_lock<std::mutex> Lock(Mutex);
				WorkCv.wait(Lock, [this]()
				{
					return bStopRequested || !Queue.empty();
				});

				if (bStopRequested && Queue.empty())
				{
					break;
				}

				Task = std::move(Queue.front());
				Queue.pop();
			}

			if (Task)
			{
				Task();
			}

			{
				std::lock_guard<std::mutex> Lock(Mutex);
				if (PendingCount > 0)
				{
					--PendingCount;
				}
				if (PendingCount == 0)
				{
					IdleCv.notify_all();
				}
			}
		}

		CATTY_CORE_INFO("WorkerPool thread {} exiting", WorkerIndex);
	}
};

FWorkerPool::FWorkerPool()
	: Impl(std::make_unique<FImpl>())
{
}

FWorkerPool::~FWorkerPool()
{
	Shutdown();
}

bool FWorkerPool::Initialize(std::size_t NumWorkers)
{
	if (bInitialized)
	{
		return true;
	}

	if (!Impl)
	{
		Impl = std::make_unique<FImpl>();
	}

	const std::size_t Count = ResolveWorkerCount(NumWorkers);
	Impl->bStopRequested = false;
	Impl->PendingCount = 0;

	Impl->Workers.reserve(Count);
	for (std::size_t Index = 0; Index < Count; ++Index)
	{
		Impl->Workers.emplace_back([this, Index]()
		{
			Impl->WorkerMain(Index);
		});
	}

	bInitialized = true;
	CATTY_CORE_INFO("WorkerPool initialized ({} threads)", Count);
	return true;
}

void FWorkerPool::Shutdown()
{
	if (!Impl)
	{
		return;
	}

	if (bInitialized)
	{
		Flush();
	}

	{
		std::lock_guard<std::mutex> Lock(Impl->Mutex);
		Impl->bStopRequested = true;
	}
	Impl->WorkCv.notify_all();

	for (std::thread& Worker : Impl->Workers)
	{
		if (Worker.joinable())
		{
			Worker.join();
		}
	}
	Impl->Workers.clear();

	{
		std::lock_guard<std::mutex> Lock(Impl->Mutex);
		while (!Impl->Queue.empty())
		{
			Impl->Queue.pop();
		}
		Impl->PendingCount = 0;
	}

	if (bInitialized)
	{
		CATTY_CORE_INFO("WorkerPool stopped");
	}
	bInitialized = false;
}

std::size_t FWorkerPool::GetNumWorkers() const
{
	return Impl ? Impl->Workers.size() : 0;
}

void FWorkerPool::Fork(std::size_t NumWorkers, const std::function<void(std::size_t WorkerIndex)>& Body)
{
	if (NumWorkers == 0 || !Body)
	{
		return;
	}

	if (!bInitialized || !Impl || NumWorkers == 1)
	{
		for (std::size_t Index = 0; Index < NumWorkers; ++Index)
		{
			Body(Index);
		}
		return;
	}

	// Caller runs index 0; pool runs [1, NumWorkers). Join only this fork group.
	std::atomic<std::uint32_t> Remaining{static_cast<std::uint32_t>(NumWorkers - 1)};
	std::mutex DoneMutex;
	std::condition_variable DoneCv;

	for (std::size_t Index = 1; Index < NumWorkers; ++Index)
	{
		Push([Body, Index, &Remaining, &DoneMutex, &DoneCv]()
		{
			Body(Index);
			if (Remaining.fetch_sub(1, std::memory_order_acq_rel) == 1)
			{
				std::lock_guard<std::mutex> Lock(DoneMutex);
				DoneCv.notify_all();
			}
		});
	}

	Body(0);

	std::unique_lock<std::mutex> Lock(DoneMutex);
	DoneCv.wait(Lock, [&Remaining]()
	{
		return Remaining.load(std::memory_order_acquire) == 0;
	});
}

void FWorkerPool::Push(std::function<void()> Task)
{
	if (!bInitialized || !Impl || !Task)
	{
		return;
	}

	{
		std::lock_guard<std::mutex> Lock(Impl->Mutex);
		if (Impl->bStopRequested)
		{
			return;
		}
		Impl->Queue.push(std::move(Task));
		++Impl->PendingCount;
	}
	Impl->WorkCv.notify_one();
}

void FWorkerPool::Flush()
{
	if (!Impl)
	{
		return;
	}

	std::unique_lock<std::mutex> Lock(Impl->Mutex);
	Impl->IdleCv.wait(Lock, [this]()
	{
		return Impl->PendingCount == 0;
	});
}

bool FWorkerPool::IsIdle() const
{
	if (!Impl || !bInitialized)
	{
		return true;
	}

	std::lock_guard<std::mutex> Lock(Impl->Mutex);
	return Impl->PendingCount == 0;
}

} // namespace Catty
