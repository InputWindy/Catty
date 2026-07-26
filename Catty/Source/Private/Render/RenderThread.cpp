#include "Catty/Render/RenderThread.h"

#include "Catty/Core/Log.h"

#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>

namespace Catty
{

struct FRenderThread::FImpl
{
	std::thread Worker;
	std::mutex Mutex;
	std::condition_variable WorkCv;
	std::condition_variable IdleCv;
	std::queue<FRenderTask> Queue;
	bool bRunning = false;
	bool bStopRequested = false;
	std::uint32_t PendingCount = 0;

	void ThreadMain()
	{
		CATTY_CORE_INFO("Render thread online");

		for (;;)
		{
			FRenderTask Task;
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

			Task();

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

		CATTY_CORE_INFO("Render thread exiting");
	}
};

FRenderThread::FRenderThread()
	: Impl(std::make_unique<FImpl>())
{
}

FRenderThread::~FRenderThread()
{
	Stop();
}

bool FRenderThread::Start(const char* ThreadName)
{
	if (Impl->bRunning)
	{
		return true;
	}

	Impl->bStopRequested = false;
	Impl->bRunning = true;
	Impl->PendingCount = 0;

	Impl->Worker = std::thread([this]()
	{
		Impl->ThreadMain();
	});

	CATTY_CORE_INFO("Render thread started ({})", ThreadName ? ThreadName : "CattyRenderThread");
	return true;
}

void FRenderThread::Stop()
{
	if (!Impl || !Impl->bRunning)
	{
		return;
	}

	{
		std::lock_guard<std::mutex> Lock(Impl->Mutex);
		Impl->bStopRequested = true;
	}
	Impl->WorkCv.notify_all();

	if (Impl->Worker.joinable())
	{
		Impl->Worker.join();
	}

	Impl->bRunning = false;
	CATTY_CORE_INFO("Render thread stopped");
}

bool FRenderThread::IsRunning() const
{
	return Impl && Impl->bRunning;
}

void FRenderThread::Enqueue(FRenderTask Task)
{
	if (!Task || !Impl)
	{
		return;
	}

	{
		std::lock_guard<std::mutex> Lock(Impl->Mutex);
		if (!Impl->bRunning || Impl->bStopRequested)
		{
			CATTY_CORE_WARN("RenderThread::Enqueue ignored (thread not running)");
			return;
		}
		Impl->Queue.push(std::move(Task));
		++Impl->PendingCount;
	}
	Impl->WorkCv.notify_one();
}

void FRenderThread::Flush()
{
	if (!Impl || !Impl->bRunning)
	{
		return;
	}

	std::unique_lock<std::mutex> Lock(Impl->Mutex);
	Impl->IdleCv.wait(Lock, [this]()
	{
		return Impl->PendingCount == 0 || Impl->bStopRequested;
	});
}

} // namespace Catty
