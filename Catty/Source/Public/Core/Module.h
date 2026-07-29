#pragma once

#include <Core/Delegate.h>
#include <Core/Export.h>

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace Catty
{

class FApp;

enum class EModuleStage : std::uint8_t
{
	PreInit,
	Init,
	PostInit,

	BeginFrame,
	ProcessInput,
	FixedUpdate,
	Update,
	LateUpdate,
	PreRender,
	Render,
	PostRender,
	EndFrame,

	/** WaitForExit drain: refuse new work, finish in-flight; FApp waits until IsIdle. */
	PrepareExit,

	Shutdown,

	NumMaxStage
};

/** Per-stage parameters. Global services go through FApp&. */
struct FStageContext
{
	float DeltaSeconds = 0.0f;
	float FixedDeltaSeconds = 0.0f;
	std::uint64_t FrameIndex = 0;
};

/**
 * Engine / plugin extension of a fixed pipeline stage.
 * Game content uses FLayer bound to PostStageDelegates instead.
 *
 * Per-stage dependencies act like barriers: for stage S, a module waits until every
 * named dependency has finished S before running ExecuteStage(S). FApp may run
 * independent modules concurrently via WorkerPool (PreferMainThread() == false).
 */
class CATTY_API IModule
{
public:
	CATTY_DECLARE_MULTICAST_DELEGATE_OneParam(FOnAttach, IModule&);
	CATTY_DECLARE_MULTICAST_DELEGATE_OneParam(FOnDetach, IModule&);
	CATTY_DECLARE_MULTICAST_DELEGATE(FOnExitRequested);

	virtual ~IModule()
	{
		Detach();
	}

	virtual const char* GetName() const = 0;

	/**
	 * Modules that must complete Stage before this module runs Stage.
	 * Empty = no barrier for that stage (registration order only for topo stability).
	 */
	virtual void GetDependencies(EModuleStage Stage, std::vector<std::string>& OutNames) const
	{
		(void)Stage;
		(void)OutNames;
	}

	/**
	 * True = ExecuteStage runs on the app/main thread (window / RHI / ImGui).
	 * False = may be dispatched to FWorkerPool for that stage.
	 */
	[[nodiscard]] virtual bool PreferMainThread() const
	{
		return true;
	}

	/**
	 * Fixed stage body (before Layers / Post broadcast).
	 * Init-family may return false to abort startup.
	 */
	virtual bool ExecuteStage(EModuleStage Stage, FApp& App, FStageContext& Ctx)
	{
		(void)Stage;
		(void)App;
		(void)Ctx;
		return true;
	}

	/**
	 * True when this module has no outstanding work (async loads, live objects, GPU, …).
	 * FApp waits for all modules + WorkerPool before Shutdown.
	 */
	[[nodiscard]] virtual bool IsIdle() const
	{
		return true;
	}

	/** Stage currently being executed / last entered on this module. */
	[[nodiscard]] EModuleStage GetCurrentStage() const { return CurrentStage; }

	[[nodiscard]] FOnExitRequested& GetOnExitRequested() { return OnExitRequested; }
	[[nodiscard]] const FOnExitRequested& GetOnExitRequested() const { return OnExitRequested; }

	void Attach()
	{
		if (bAttached)
		{
			return;
		}
		bAttached = true;
		AttachEvent.Broadcast(*this);
		OnAttach();
	}

	void Detach()
	{
		if (!bAttached)
		{
			return;
		}
		bAttached = false;
		DetachEvent.Broadcast(*this);
		OnDetach();
	}

	[[nodiscard]] bool IsAttached() const { return bAttached; }

	[[nodiscard]] FOnAttach& GetOnAttach() { return AttachEvent; }
	[[nodiscard]] const FOnAttach& GetOnAttach() const { return AttachEvent; }
	[[nodiscard]] FOnDetach& GetOnDetach() { return DetachEvent; }
	[[nodiscard]] const FOnDetach& GetOnDetach() const { return DetachEvent; }

protected:
	virtual void OnAttach() {}
	virtual void OnDetach() {}

	FOnExitRequested OnExitRequested;

private:
	friend class FApp;

	void SetCurrentStage(EModuleStage Stage) { CurrentStage = Stage; }

	void ResetStageFence()
	{
		std::lock_guard<std::mutex> Lock(StageMutex);
		bStageComplete = false;
	}

	void SignalStageComplete()
	{
		{
			std::lock_guard<std::mutex> Lock(StageMutex);
			bStageComplete = true;
		}
		StageCv.notify_all();
	}

	void WaitStageComplete()
	{
		std::unique_lock<std::mutex> Lock(StageMutex);
		StageCv.wait(Lock, [this]()
		{
			return bStageComplete;
		});
	}

	bool bAttached = false;
	bool bStageComplete = true;
	EModuleStage CurrentStage = EModuleStage::NumMaxStage;
	mutable std::mutex StageMutex;
	std::condition_variable StageCv;
	FOnAttach AttachEvent;
	FOnDetach DetachEvent;
};

} // namespace Catty
