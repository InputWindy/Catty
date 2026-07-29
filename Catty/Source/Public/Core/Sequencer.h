#pragma once

/**
 * Type-agnostic lockstep Sequencer suite (header-only templates).
 *
 * Building blocks:
 *   - TThreadSafeQueue<T>           MPMC queue
 *   - TPipelineContext<TObject>     opaque FPipelineObjectKey → object
 *   - FPipelineCommandReply         Done() / Wait() completion token
 *   - TPipelineCommand<TStage,TPayload>
 *   - TSequencerDep<TSrcTraits,TDstTraits>  enum pin wiring (FromTo map)
 *   - ISequencerLockstep            type-erased pin/gate surface for cross-seq wiring
 *   - TSequencer<TTraits>           per-object threads, StartToken gate, Complete → RaisePin
 *
 * TTraits requirements:
 *   using FStage;                    // enum (class) listing this sequencer's stages
 *   using FObject;                   // PreferMainThread(), OnSequencerStage(FStage)
 *   using FPayload;
 *   static constexpr std::size_t NumStages;
 *   static constexpr std::size_t StageToIndex(FStage);
 *   static constexpr FStage IndexToStage(std::size_t);
 *
 * Cross-sequencer lockstep uses TSequencerDep::FromTo (Src::FStage → Dst::FStage).
 * Same-sequencer business edges use Enqueue / EnqueueAndListen with SourceKey.
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Catty
{

/** Opaque id inside one TPipelineContext / TSequencer. 0 = invalid. */
using FPipelineObjectKey = std::uint64_t;

// ---------------------------------------------------------------------------
// TThreadSafeQueue
// ---------------------------------------------------------------------------

template <typename T>
class TThreadSafeQueue
{
public:
	void Push(T Item)
	{
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			if (bClosed)
			{
				return;
			}
			Queue.push(std::move(Item));
		}
		CV.notify_one();
	}

	[[nodiscard]] bool TryPop(T& Out)
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		if (Queue.empty())
		{
			return false;
		}
		Out = std::move(Queue.front());
		Queue.pop();
		return true;
	}

	/** Blocks until an item is available or the queue is Closed (empty). Returns false if closed+empty. */
	[[nodiscard]] bool PopWait(T& Out)
	{
		std::unique_lock<std::mutex> Lock(Mutex);
		CV.wait(Lock, [this]()
		{
			return bClosed || !Queue.empty();
		});
		if (Queue.empty())
		{
			return false;
		}
		Out = std::move(Queue.front());
		Queue.pop();
		return true;
	}

	void Close()
	{
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			bClosed = true;
		}
		CV.notify_all();
	}

	[[nodiscard]] bool IsClosed() const
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		return bClosed;
	}

	[[nodiscard]] std::size_t GetSize() const
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		return Queue.size();
	}

private:
	mutable std::mutex Mutex;
	std::condition_variable CV;
	std::queue<T> Queue;
	bool bClosed = false;
};

// ---------------------------------------------------------------------------
// Context
// ---------------------------------------------------------------------------

template <typename TObject>
class TPipelineContext
{
public:
	[[nodiscard]] FPipelineObjectKey Register(TObject& Object)
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		const FPipelineObjectKey Key = NextKey++;
		if (NextKey == 0)
		{
			NextKey = 1;
		}
		Objects.emplace(Key, &Object);
		return Key;
	}

	void Unregister(FPipelineObjectKey Key)
	{
		if (Key == 0)
		{
			return;
		}
		std::lock_guard<std::mutex> Lock(Mutex);
		Objects.erase(Key);
	}

	[[nodiscard]] TObject* Find(FPipelineObjectKey Key) const
	{
		if (Key == 0)
		{
			return nullptr;
		}
		std::lock_guard<std::mutex> Lock(Mutex);
		const auto It = Objects.find(Key);
		return It == Objects.end() ? nullptr : It->second;
	}

	[[nodiscard]] std::size_t GetObjectCount() const
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		return Objects.size();
	}

	template <typename TFunc>
	void ForEachObject(TFunc&& Func) const
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		for (const auto& Pair : Objects)
		{
			if (Pair.second)
			{
				Func(*Pair.second);
			}
		}
	}

private:
	mutable std::mutex Mutex;
	std::unordered_map<FPipelineObjectKey, TObject*> Objects;
	FPipelineObjectKey NextKey = 1;
};

// ---------------------------------------------------------------------------
// Command + Reply
// ---------------------------------------------------------------------------

class FPipelineCommandReply
{
public:
	void Done()
	{
		{
			std::lock_guard<std::mutex> Lock(Mutex);
			if (bDone)
			{
				return;
			}
			bDone = true;
		}
		CV.notify_all();
	}

	void Wait()
	{
		std::unique_lock<std::mutex> Lock(Mutex);
		CV.wait(Lock, [this]()
		{
			return bDone;
		});
	}

	[[nodiscard]] bool IsDone() const
	{
		std::lock_guard<std::mutex> Lock(Mutex);
		return bDone;
	}

private:
	mutable std::mutex Mutex;
	std::condition_variable CV;
	bool bDone = false;
};

/** RAII: always Done() when leaving a drain/handle scope. */
class FPipelineCommandReplyGuard
{
public:
	explicit FPipelineCommandReplyGuard(std::shared_ptr<FPipelineCommandReply> InReply)
		: Reply(std::move(InReply))
	{
	}

	~FPipelineCommandReplyGuard()
	{
		if (Reply)
		{
			Reply->Done();
		}
	}

	FPipelineCommandReplyGuard(const FPipelineCommandReplyGuard&) = delete;
	FPipelineCommandReplyGuard& operator=(const FPipelineCommandReplyGuard&) = delete;

	FPipelineCommandReplyGuard(FPipelineCommandReplyGuard&& Other) noexcept
		: Reply(std::move(Other.Reply))
	{
	}

	FPipelineCommandReplyGuard& operator=(FPipelineCommandReplyGuard&& Other) noexcept
	{
		if (this != &Other)
		{
			if (Reply)
			{
				Reply->Done();
			}
			Reply = std::move(Other.Reply);
		}
		return *this;
	}

	void ReleaseWithoutDone()
	{
		Reply.reset();
	}

private:
	std::shared_ptr<FPipelineCommandReply> Reply;
};

template <typename TStage, typename TPayload>
struct TPipelineCommand
{
	FPipelineObjectKey SourceKey = 0;
	TStage SourceStage{};
	TStage TargetStage{};
	TPayload Payload{};
	std::shared_ptr<FPipelineCommandReply> Reply; // null = fire-and-forget
};

// ---------------------------------------------------------------------------
// Stage-enum pin dependency (Src pin → Dst gate)
// ---------------------------------------------------------------------------

/** Hash for enum / enum class keys in TSequencerDep::FromTo. */
struct FEnumClassHash
{
	template <typename TEnum>
	[[nodiscard]] std::size_t operator()(TEnum Value) const noexcept
	{
		using FUnderlying = std::underlying_type_t<TEnum>;
		return static_cast<std::size_t>(static_cast<FUnderlying>(Value));
	}
};

/**
 * Cross-sequencer lockstep edges keyed by each side's Traits::FStage enum.
 * Stages themselves are defined on Traits (FStage + NumStages + StageToIndex).
 *
 * Example:
 *   TSequencerDep<FModuleTraits, FLayerTraits> Dep;
 *   Dep.FromTo[FModuleTraits::FStage::BeginFrame] = FLayerTraits::FStage::BeginFrame;
 *   Dep.FromTo[FModuleTraits::FStage::Render] = FLayerTraits::FStage::Render;
 *   BindSequencerDep(ModuleSeq, LayerSeq, Dep);
 */
template <typename TSrcTraits, typename TDstTraits>
struct TSequencerDep
{
	using FSrcStage = typename TSrcTraits::FStage;
	using FDstStage = typename TDstTraits::FStage;

	/** Source output pin (Src stage) → destination gate (Dst stage). */
	std::unordered_map<FSrcStage, FDstStage, FEnumClassHash> FromTo;
};

/**
 * Type-erased lockstep surface so FApp can hold sequencer pointers uniformly.
 * Typed wiring uses BindSequencerDep with TSequencerDep (stage enums).
 */
class ISequencerLockstep
{
public:
	virtual ~ISequencerLockstep() = default;

	[[nodiscard]] virtual std::size_t GetNumStages() const = 0;

	/** Accumulate how many external pins GateStageIndex must hear before StartToken. */
	virtual void BindGateExternalExpect(std::size_t GateStageIndex, std::size_t AdditionalExpect) = 0;

	/** One external pin arrived for this gate (called by wiring fan-out). */
	virtual void NotifyExternalPin(std::size_t GateStageIndex) = 0;

	/** Listen for this sequencer raising a stage pin (StageIndex). */
	virtual void AddPinRaiseListener(std::function<void(std::size_t StageIndex)> Listener) = 0;
};

// ---------------------------------------------------------------------------
// TSequencer
// ---------------------------------------------------------------------------

/**
 * Lockstep sequencer: listen external pins → StartToken to all objects →
 * wait all Complete → RaiseStagePin (pin = Traits::FStage).
 *
 * FObject must provide:
 *   bool PreferMainThread() const;
 *   void OnSequencerStage(FStage Stage);
 */
template <typename TTraits>
class TSequencer : public ISequencerLockstep
{
public:
	using FStage = typename TTraits::FStage;
	using FObject = typename TTraits::FObject;
	using FPayload = typename TTraits::FPayload;
	using FCommand = TPipelineCommand<FStage, FPayload>;
	using FQueue = TThreadSafeQueue<FCommand>;

	static constexpr std::size_t NumStages = TTraits::NumStages;

	TSequencer()
	{
		for (std::size_t I = 0; I < NumStages; ++I)
		{
			GateStates[I].ExpectedExternal = 0;
			GateStates[I].ReceivedExternal = 0;
			GateStates[I].StartGeneration = 0;
			GateStates[I].CompleteCount = 0;
			GateStates[I].bPinRaisedThisRound = false;
		}
	}

	~TSequencer() override
	{
		RequestStop();
		WaitForStop();
	}

	TSequencer(const TSequencer&) = delete;
	TSequencer& operator=(const TSequencer&) = delete;

	[[nodiscard]] TPipelineContext<FObject>& GetContext()
	{
		return Context;
	}

	[[nodiscard]] const TPipelineContext<FObject>& GetContext() const
	{
		return Context;
	}

	[[nodiscard]] FPipelineObjectKey Register(FObject& Object)
	{
		std::lock_guard<std::mutex> Lock(RegistryMutex);
		const FPipelineObjectKey Key = Context.Register(Object);
		FObjectSlot Slot;
		Slot.Object = &Object;
		Slot.Key = Key;
		Slot.Mailboxes = std::make_unique<std::array<FQueue, NumStages>>();
		Slot.SeenGenerations = std::make_unique<std::array<std::uint64_t, NumStages>>();
		Slot.SeenGenerations->fill(0);
		Slot.bPreferMainThread = Object.PreferMainThread();
		ObjectsByKey.emplace(Key, std::move(Slot));
		OrderedKeys.push_back(Key);
		RegisteredObjectCount.store(OrderedKeys.size(), std::memory_order_release);
		return Key;
	}

	void Unregister(FPipelineObjectKey Key)
	{
		std::lock_guard<std::mutex> Lock(RegistryMutex);
		Context.Unregister(Key);
		ObjectsByKey.erase(Key);
		OrderedKeys.erase(
			std::remove(OrderedKeys.begin(), OrderedKeys.end(), Key),
			OrderedKeys.end());
		RegisteredObjectCount.store(OrderedKeys.size(), std::memory_order_release);
	}

	[[nodiscard]] FObject* Find(FPipelineObjectKey Key) const
	{
		return Context.Find(Key);
	}

	[[nodiscard]] FQueue* GetMailbox(FPipelineObjectKey Key, FStage Stage)
	{
		std::lock_guard<std::mutex> Lock(RegistryMutex);
		const auto It = ObjectsByKey.find(Key);
		if (It == ObjectsByKey.end() || !It->second.Mailboxes)
		{
			return nullptr;
		}
		const std::size_t Index = TTraits::StageToIndex(Stage);
		if (Index >= NumStages)
		{
			return nullptr;
		}
		return &(*It->second.Mailboxes)[Index];
	}

	void Enqueue(FPipelineObjectKey SourceKey, FStage SourceStage, FPipelineObjectKey TargetKey, FStage TargetStage, FPayload Payload)
	{
		FCommand Cmd;
		Cmd.SourceKey = SourceKey;
		Cmd.SourceStage = SourceStage;
		Cmd.TargetStage = TargetStage;
		Cmd.Payload = std::move(Payload);
		Cmd.Reply = nullptr;
		PushCommand(TargetKey, TargetStage, std::move(Cmd));
	}

	[[nodiscard]] std::shared_ptr<FPipelineCommandReply> EnqueueAndListen(
		FPipelineObjectKey SourceKey,
		FStage SourceStage,
		FPipelineObjectKey TargetKey,
		FStage TargetStage,
		FPayload Payload)
	{
		auto Reply = std::make_shared<FPipelineCommandReply>();
		FCommand Cmd;
		Cmd.SourceKey = SourceKey;
		Cmd.SourceStage = SourceStage;
		Cmd.TargetStage = TargetStage;
		Cmd.Payload = std::move(Payload);
		Cmd.Reply = Reply;
		{
			std::lock_guard<std::mutex> Lock(ListenMutex);
			OutstandingBySource[SourceKey].push_back(Reply);
		}
		PushCommand(TargetKey, TargetStage, std::move(Cmd));
		return Reply;
	}

	void WaitOutstandingListens(FPipelineObjectKey SourceKey)
	{
		std::vector<std::shared_ptr<FPipelineCommandReply>> Local;
		{
			std::lock_guard<std::mutex> Lock(ListenMutex);
			Local.swap(OutstandingBySource[SourceKey]);
		}
		for (const std::shared_ptr<FPipelineCommandReply>& Reply : Local)
		{
			if (Reply)
			{
				Reply->Wait();
			}
		}
	}

	/** Drain TargetKey's mailbox for Stage; each command's Reply is Done via RAII. */
	template <typename THandler>
	void DrainMailbox(FPipelineObjectKey TargetKey, FStage Stage, THandler&& Handler)
	{
		FQueue* Queue = GetMailbox(TargetKey, Stage);
		if (!Queue)
		{
			return;
		}
		FCommand Cmd;
		while (Queue->TryPop(Cmd))
		{
			FPipelineCommandReplyGuard Guard(Cmd.Reply);
			Handler(Cmd);
		}
	}

	// --- ISequencerLockstep ------------------------------------------------

	[[nodiscard]] std::size_t GetNumStages() const override
	{
		return NumStages;
	}

	void BindGateExternalExpect(std::size_t GateStageIndex, std::size_t AdditionalExpect) override
	{
		if (GateStageIndex >= NumStages || AdditionalExpect == 0)
		{
			return;
		}
		std::lock_guard<std::mutex> Lock(GateMutex);
		GateStates[GateStageIndex].ExpectedExternal += AdditionalExpect;
	}

	void NotifyExternalPin(std::size_t GateStageIndex) override
	{
		if (GateStageIndex >= NumStages)
		{
			return;
		}
		bool bShouldOpen = false;
		bool bRaiseImmediate = false;
		{
			std::lock_guard<std::mutex> Lock(GateMutex);
			FGateState& Gate = GateStates[GateStageIndex];
			++Gate.ReceivedExternal;
			if (Gate.ReceivedExternal >= Gate.ExpectedExternal)
			{
				bShouldOpen = TryOpenGate_NoLock(GateStageIndex, &bRaiseImmediate);
			}
		}
		if (bShouldOpen)
		{
			GateCV.notify_all();
		}
		if (bRaiseImmediate)
		{
			RaiseStagePin(GateStageIndex);
		}
	}

	void AddPinRaiseListener(std::function<void(std::size_t StageIndex)> Listener) override
	{
		if (!Listener)
		{
			return;
		}
		std::lock_guard<std::mutex> Lock(PinListenerMutex);
		PinListeners.push_back(std::move(Listener));
	}

	/**
	 * Open gates that need zero external pins (e.g. Module stage 0).
	 * Call once after BindSequencerWiring / BindGateExternalExpect, before StartMainLoops.
	 * Vacuous (zero-object) sequencers raise pins immediately for those gates.
	 */
	void OpenZeroExpectGates()
	{
		std::vector<std::size_t> Opened;
		std::vector<std::size_t> RaiseImmediate;
		{
			std::lock_guard<std::mutex> Lock(GateMutex);
			for (std::size_t I = 0; I < NumStages; ++I)
			{
				if (GateStates[I].ExpectedExternal == 0)
				{
					bool bRaise = false;
					if (TryOpenGate_NoLock(I, &bRaise))
					{
						Opened.push_back(I);
						if (bRaise)
						{
							RaiseImmediate.push_back(I);
						}
					}
				}
			}
		}
		if (!Opened.empty())
		{
			GateCV.notify_all();
		}
		for (std::size_t StageIndex : RaiseImmediate)
		{
			RaiseStagePin(StageIndex);
		}
	}

	void SignalStageComplete(FPipelineObjectKey /*ObjectKey*/, FStage Stage)
	{
		const std::size_t StageIndex = TTraits::StageToIndex(Stage);
		if (StageIndex >= NumStages)
		{
			return;
		}

		bool bRaisePin = false;
		{
			std::lock_guard<std::mutex> Lock(GateMutex);
			FGateState& Gate = GateStates[StageIndex];
			++Gate.CompleteCount;
			const std::size_t NumObjects = RegisteredObjectCount.load(std::memory_order_acquire);
			if (!Gate.bPinRaisedThisRound && NumObjects > 0 && Gate.CompleteCount >= NumObjects)
			{
				Gate.bPinRaisedThisRound = true;
				bRaisePin = true;
				Gate.ReceivedExternal = 0;
			}
		}

		if (bRaisePin)
		{
			RaiseStagePin(StageIndex);
			// Re-arm zero-expect gates for the next frame (N>0; no vacuous spin).
			bool bReopened = false;
			{
				std::lock_guard<std::mutex> Lock(GateMutex);
				FGateState& Gate = GateStates[StageIndex];
				if (Gate.ExpectedExternal == 0
					&& RegisteredObjectCount.load(std::memory_order_acquire) > 0)
				{
					bReopened = TryOpenGate_NoLock(StageIndex);
				}
			}
			if (bReopened)
			{
				GateCV.notify_all();
			}
		}
	}

	void WaitStartToken(FStage Stage, std::uint64_t& InOutSeenGeneration)
	{
		const std::size_t StageIndex = TTraits::StageToIndex(Stage);
		std::unique_lock<std::mutex> Lock(GateMutex);
		GateCV.wait(Lock, [this, StageIndex, &InOutSeenGeneration]()
		{
			return bStopRequested
				|| GateStates[StageIndex].StartGeneration > InOutSeenGeneration;
		});
		if (!bStopRequested)
		{
			InOutSeenGeneration = GateStates[StageIndex].StartGeneration;
		}
	}

	void RequestStop()
	{
		{
			std::lock_guard<std::mutex> Lock(GateMutex);
			bStopRequested = true;
		}
		GateCV.notify_all();
		CloseAllMailboxes();
	}

	[[nodiscard]] bool IsStopRequested() const
	{
		std::lock_guard<std::mutex> Lock(GateMutex);
		return bStopRequested;
	}

	/**
	 * Spawn one thread per PreferMainThread==false object.
	 * Main-thread objects are tracked for RunMainThreadObjectLoops / PumpOnce.
	 */
	void StartWorkerLoops()
	{
		std::lock_guard<std::mutex> Lock(RegistryMutex);
		WorkerThreads.clear();
		MainThreadKeys.clear();

		for (FPipelineObjectKey Key : OrderedKeys)
		{
			auto It = ObjectsByKey.find(Key);
			if (It == ObjectsByKey.end() || !It->second.Object)
			{
				continue;
			}
			if (It->second.bPreferMainThread)
			{
				MainThreadKeys.push_back(Key);
				continue;
			}
			FObject* Object = It->second.Object;
			WorkerThreads.emplace_back([this, Key, Object]()
			{
				RunObjectLoop(Key, *Object);
			});
		}
	}

	/** Block until PreferMainThread objects finish one full stage pass (all stages once). */
	void RunMainThreadObjectLoopsOnce()
	{
		std::vector<FPipelineObjectKey> Keys;
		{
			std::lock_guard<std::mutex> Lock(RegistryMutex);
			Keys = MainThreadKeys;
		}
		for (FPipelineObjectKey Key : Keys)
		{
			FObject* Object = nullptr;
			{
				std::lock_guard<std::mutex> Lock(RegistryMutex);
				const auto It = ObjectsByKey.find(Key);
				if (It != ObjectsByKey.end())
				{
					Object = It->second.Object;
				}
			}
			if (Object)
			{
				RunObjectLoopOnce(Key, *Object);
			}
		}
	}

	/** Continuous main-thread loops until stop (call from app main thread). */
	void RunMainThreadObjectLoops()
	{
		std::vector<FPipelineObjectKey> Keys;
		{
			std::lock_guard<std::mutex> Lock(RegistryMutex);
			Keys = MainThreadKeys;
		}
		if (Keys.empty())
		{
			return;
		}
		// Single main-thread object: dedicated loop. Multiple: round-robin one stage-pass each.
		if (Keys.size() == 1)
		{
			FObject* Object = Find(Keys[0]);
			if (Object)
			{
				RunObjectLoop(Keys[0], *Object);
			}
			return;
		}
		while (!IsStopRequested())
		{
			for (FPipelineObjectKey Key : Keys)
			{
				if (IsStopRequested())
				{
					break;
				}
				FObject* Object = Find(Key);
				if (Object)
				{
					RunObjectLoopOnce(Key, *Object);
				}
			}
		}
	}

	void WaitForStop()
	{
		for (std::thread& Thread : WorkerThreads)
		{
			if (Thread.joinable())
			{
				Thread.join();
			}
		}
		WorkerThreads.clear();
	}

	[[nodiscard]] bool AreWorkersJoinableEmpty() const
	{
		return WorkerThreads.empty();
	}

private:
	struct FGateState
	{
		std::size_t ExpectedExternal = 0;
		std::size_t ReceivedExternal = 0;
		std::uint64_t StartGeneration = 0;
		std::size_t CompleteCount = 0;
		bool bPinRaisedThisRound = false;
	};

	struct FObjectSlot
	{
		FObject* Object = nullptr;
		FPipelineObjectKey Key = 0;
		std::unique_ptr<std::array<FQueue, NumStages>> Mailboxes;
		std::unique_ptr<std::array<std::uint64_t, NumStages>> SeenGenerations;
		bool bPreferMainThread = true;
	};

	/** Caller holds GateMutex. Returns true if StartToken was issued.
	 *  OutRaisePinImmediate: set when there are zero registered objects (vacuous complete). */
	bool TryOpenGate_NoLock(std::size_t GateStageIndex, bool* OutRaisePinImmediate = nullptr)
	{
		if (OutRaisePinImmediate)
		{
			*OutRaisePinImmediate = false;
		}
		FGateState& Gate = GateStates[GateStageIndex];
		if (Gate.ExpectedExternal > 0 && Gate.ReceivedExternal < Gate.ExpectedExternal)
		{
			return false;
		}
		// Avoid double-open in the same round before pin raise.
		if (Gate.CompleteCount > 0 && !Gate.bPinRaisedThisRound)
		{
			return false;
		}
		Gate.StartGeneration++;
		Gate.CompleteCount = 0;
		Gate.bPinRaisedThisRound = false;
		Gate.ReceivedExternal = 0;

		const std::size_t NumObjects = RegisteredObjectCount.load(std::memory_order_acquire);
		if (NumObjects == 0)
		{
			Gate.bPinRaisedThisRound = true;
			if (OutRaisePinImmediate)
			{
				*OutRaisePinImmediate = true;
			}
		}
		return true;
	}

	void PushCommand(FPipelineObjectKey TargetKey, FStage TargetStage, FCommand Cmd)
	{
		FQueue* Queue = GetMailbox(TargetKey, TargetStage);
		if (!Queue)
		{
			if (Cmd.Reply)
			{
				Cmd.Reply->Done();
			}
			return;
		}
		Queue->Push(std::move(Cmd));
	}

	void RaiseStagePin(std::size_t StageIndex)
	{
		std::vector<std::function<void(std::size_t)>> Listeners;
		{
			std::lock_guard<std::mutex> Lock(PinListenerMutex);
			Listeners = PinListeners;
		}
		for (const auto& Listener : Listeners)
		{
			Listener(StageIndex);
		}
	}

	void CloseAllMailboxes()
	{
		std::lock_guard<std::mutex> Lock(RegistryMutex);
		for (auto& Pair : ObjectsByKey)
		{
			if (!Pair.second.Mailboxes)
			{
				continue;
			}
			for (FQueue& Queue : *Pair.second.Mailboxes)
			{
				Queue.Close();
			}
		}
	}

	std::array<std::uint64_t, NumStages>* GetSeenGenerations(FPipelineObjectKey Key)
	{
		std::lock_guard<std::mutex> Lock(RegistryMutex);
		const auto It = ObjectsByKey.find(Key);
		if (It == ObjectsByKey.end() || !It->second.SeenGenerations)
		{
			return nullptr;
		}
		return It->second.SeenGenerations.get();
	}

	void RunObjectLoop(FPipelineObjectKey Key, FObject& Object)
	{
		while (!IsStopRequested())
		{
			if (!RunObjectLoopOnce(Key, Object))
			{
				return;
			}
		}
	}

	/** One full pass over all stages. Returns false if stopped mid-pass. */
	bool RunObjectLoopOnce(FPipelineObjectKey Key, FObject& Object)
	{
		std::array<std::uint64_t, NumStages>* SeenGen = GetSeenGenerations(Key);
		if (!SeenGen)
		{
			return false;
		}
		for (std::size_t StageIndex = 0; StageIndex < NumStages; ++StageIndex)
		{
			if (IsStopRequested())
			{
				return false;
			}
			const FStage Stage = TTraits::IndexToStage(StageIndex);
			WaitStartToken(Stage, (*SeenGen)[StageIndex]);
			if (IsStopRequested())
			{
				return false;
			}
			WaitOutstandingListens(Key);
			Object.OnSequencerStage(Stage);
			SignalStageComplete(Key, Stage);
		}
		return true;
	}

	TPipelineContext<FObject> Context;

	mutable std::mutex RegistryMutex;
	std::unordered_map<FPipelineObjectKey, FObjectSlot> ObjectsByKey;
	std::vector<FPipelineObjectKey> OrderedKeys;
	std::vector<FPipelineObjectKey> MainThreadKeys;
	std::vector<std::thread> WorkerThreads;
	std::atomic<std::size_t> RegisteredObjectCount{0};

	mutable std::mutex GateMutex;
	std::condition_variable GateCV;
	std::array<FGateState, NumStages> GateStates{};
	bool bStopRequested = false;

	std::mutex PinListenerMutex;
	std::vector<std::function<void(std::size_t)>> PinListeners;

	std::mutex ListenMutex;
	std::unordered_map<FPipelineObjectKey, std::vector<std::shared_ptr<FPipelineCommandReply>>> OutstandingBySource;
};

/**
 * Bind TSequencerDep: each Src stage pin raise notifies the mapped Dst gate.
 * Stages outside NumStages (via StageToIndex) are skipped.
 */
template <typename TSrcTraits, typename TDstTraits>
void BindSequencerDep(
	TSequencer<TSrcTraits>& Src,
	TSequencer<TDstTraits>& Dst,
	const TSequencerDep<TSrcTraits, TDstTraits>& Dep)
{
	for (const auto& Pair : Dep.FromTo)
	{
		const std::size_t SourcePin = TSrcTraits::StageToIndex(Pair.first);
		const std::size_t TargetGate = TDstTraits::StageToIndex(Pair.second);
		if (SourcePin >= TSrcTraits::NumStages || TargetGate >= TDstTraits::NumStages)
		{
			continue;
		}

		Dst.BindGateExternalExpect(TargetGate, 1);
		Src.AddPinRaiseListener([&Dst, SourcePin, TargetGate](std::size_t RaisedStageIndex)
		{
			if (RaisedStageIndex == SourcePin)
			{
				Dst.NotifyExternalPin(TargetGate);
			}
		});
	}
}

} // namespace Catty
