#pragma once

#include <Core/Delegate.h>
#include <Core/Export.h>
#include <Core/FrameStage.h>

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
 * Engine / plugin extension.
 * Game content uses FLayer on the Layer SequenceGraph sequencer.
 *
 * Lifecycle (PreInit/Init/PostInit/Shutdown): FApp::ExecuteLifecycleStage runs modules
 * in dependency order (GetDependencies) and may use WorkerPool for PreferMainThread==false.
 *
 * Frame: SequenceGraph lockstep calls OnSequencerStage on each module; that invokes
 * ExecuteStage for the overlapping EModuleStage. Cross-module frame order is the
 * Sequencer gate graph, not GetDependencies.
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
	 * Used by FApp lifecycle ExecuteLifecycleStage barriers only (not frame Sequencer).
	 */
	virtual void GetDependencies(EModuleStage Stage, std::vector<std::string>& OutNames) const
	{
		(void)Stage;
		(void)OutNames;
	}

	/**
	 * True = PreferMainThread Sequencer loop / lifecycle main-thread path.
	 * False = Sequencer worker thread (and lifecycle WorkerPool jobs).
	 */
	[[nodiscard]] virtual bool PreferMainThread() const
	{
		return true;
	}

	/**
	 * Stage body. Lifecycle: FApp::ExecuteLifecycleStage. Frame: OnSequencerStage.
	 * Init-family may return false to abort startup.
	 */
	virtual bool ExecuteStage(EModuleStage Stage, FApp& App, FStageContext& Ctx)
	{
		(void)Stage;
		(void)App;
		(void)Ctx;
		return true;
	}

	/** Frame SequenceGraph entry; maps overlapping stages to ExecuteStage. */
	virtual void OnSequencerStage(EFrameStage Stage);

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

/** Maps overlapping frame stages to EModuleStage; Attach/Detach → NumMaxStage. */
[[nodiscard]] inline EModuleStage FrameStageToModuleStage(EFrameStage Stage)
{
	switch (Stage)
	{
	case EFrameStage::BeginFrame: return EModuleStage::BeginFrame;
	case EFrameStage::ProcessInput: return EModuleStage::ProcessInput;
	case EFrameStage::FixedUpdate: return EModuleStage::FixedUpdate;
	case EFrameStage::Update: return EModuleStage::Update;
	case EFrameStage::LateUpdate: return EModuleStage::LateUpdate;
	case EFrameStage::PreRender: return EModuleStage::PreRender;
	case EFrameStage::Render: return EModuleStage::Render;
	case EFrameStage::PostRender: return EModuleStage::PostRender;
	case EFrameStage::EndFrame: return EModuleStage::EndFrame;
	case EFrameStage::PrepareExit: return EModuleStage::PrepareExit;
	default: return EModuleStage::NumMaxStage;
	}
}

} // namespace Catty
