#pragma once

/**
 * TSequenceGraph: owns up to 10 typed TSequencer slots (A..J) plus one
 * cross-sequencer thread-safe TContext (first template parameter).
 *
 * Unused slots default to void (no sequencer instance).
 *
 * Example:
 * ```
 *   using FGraph = Catty::TSequenceGraph<FMyApp, FModuleTraits, FLayerTraits>; // CRTP: FMyApp : FGraph
 *   FGraph& Graph = MyApp;
 *   Graph.WithContext([](FMyApp& App) { App.DoWork(); });
 *   auto Key = Graph.GetA().Register(Module);
 *   Catty::TSequencerDep<FModuleTraits, FLayerTraits> Dep;
 *   Dep.FromTo[FModuleTraits::FStage::BeginFrame] = FLayerTraits::FStage::BeginFrame;
 *   Graph.BindDep(Graph.GetA(), Graph.GetB(), Dep);
 *   if (!Graph.Build()) { abort on cycle; }
 *   Graph.Execute(); // Running → main/worker loops → Stopped
 * ```
 */

#include <Core/Sequencer.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <type_traits>
#include <utility>
#include <vector>

namespace Catty
{

/**
 * Same lifecycle phases as EAppState (kept local so SequenceGraph does not include App.h).
 */
enum class ESequenceGraphState : std::uint8_t
{
	/** Before / after Execute — not running. */
	Stopped = 0,
	/** Main loops accepting normal work. */
	Running,
	/** Exit requested: drain until idle, then shut down. */
	WaitForExit,
	/** Shutdown / join in progress. */
	ShuttingDown,
};

namespace Private
{

template <typename TTraits>
struct TSequenceGraphSlot
{
	static constexpr bool bIsActive = true;
	using FTraits = TTraits;

	TSequencer<TTraits> Sequencer;

	[[nodiscard]] TSequencer<TTraits>& Get()
	{
		return Sequencer;
	}

	[[nodiscard]] const TSequencer<TTraits>& Get() const
	{
		return Sequencer;
	}

	[[nodiscard]] ISequencerLockstep* AsLockstep()
	{
		return &Sequencer;
	}

	[[nodiscard]] const ISequencerLockstep* AsLockstep() const
	{
		return &Sequencer;
	}

	void StartWorkerLoops()
	{
		Sequencer.StartWorkerLoops();
	}

	void OpenZeroExpectGates()
	{
		Sequencer.OpenZeroExpectGates();
	}

	void RequestStop()
	{
		Sequencer.RequestStop();
	}

	void WaitForStop()
	{
		Sequencer.WaitForStop();
	}

	void RunMainThreadObjectLoops()
	{
		Sequencer.RunMainThreadObjectLoops();
	}

	[[nodiscard]] bool TryPumpMainThreadObjectsOnce()
	{
		return Sequencer.TryPumpMainThreadObjectsOnce();
	}

	void WaitForGateNotify(std::chrono::milliseconds Timeout)
	{
		Sequencer.WaitForGateNotify(Timeout);
	}

	[[nodiscard]] bool IsStopRequested() const
	{
		return Sequencer.IsStopRequested();
	}
};

template <>
struct TSequenceGraphSlot<void>
{
	static constexpr bool bIsActive = false;

	[[nodiscard]] ISequencerLockstep* AsLockstep()
	{
		return nullptr;
	}

	[[nodiscard]] const ISequencerLockstep* AsLockstep() const
	{
		return nullptr;
	}

	void StartWorkerLoops()
	{
	}

	void OpenZeroExpectGates()
	{
	}

	void RequestStop()
	{
	}

	void WaitForStop()
	{
	}

	void RunMainThreadObjectLoops()
	{
	}

	[[nodiscard]] bool TryPumpMainThreadObjectsOnce()
	{
		return false;
	}

	void WaitForGateNotify(std::chrono::milliseconds /*Timeout*/)
	{
	}

	[[nodiscard]] bool IsStopRequested() const
	{
		return true;
	}
};

} // namespace Private

/**
 * Graph of sequencers + shared cross-sequence context.
 *
 * Template parameters:
 *   TContext — shared state visible across all sequencers (thread-safe via WithContext / AccessContext)
 *   A..J     — sequencer Traits types (void = empty slot); pin deps use TSequencerDep + BindDep
 */
template <
	typename TContext,
	typename A = void,
	typename B = void,
	typename C = void,
	typename D = void,
	typename E = void,
	typename F = void,
	typename G = void,
	typename H = void,
	typename TI = void,
	typename TJ = void>
class TSequenceGraph
{
public:
	using FContext = TContext;

	TSequenceGraph() = default;

	TSequenceGraph(const TSequenceGraph&) = delete;
	TSequenceGraph& operator=(const TSequenceGraph&) = delete;

	virtual ~TSequenceGraph()
	{
		if (GraphState == ESequenceGraphState::Running
			|| GraphState == ESequenceGraphState::WaitForExit)
		{
			RequestWaitForExit();
		}
		if (GraphState != ESequenceGraphState::Stopped)
		{
			GraphState = ESequenceGraphState::ShuttingDown;
			RequestStopAll();
			WaitForStopAll();
			GraphState = ESequenceGraphState::Stopped;
		}
	}

	[[nodiscard]] ESequenceGraphState GetState() const
	{
		return GraphState;
	}

	[[nodiscard]] bool IsRunning() const
	{
		return GraphState == ESequenceGraphState::Running;
	}

	[[nodiscard]] bool IsBuilt() const
	{
		return bBuilt;
	}

	// --- Cross-sequence context (thread-safe) --------------------------------

	/** Run Func(*this as TContext) under the graph context mutex (CRTP). */
	template <typename TFunc>
	decltype(auto) WithContext(TFunc&& Func)
	{
		std::lock_guard<std::mutex> Lock(ContextMutex);
		return std::forward<TFunc>(Func)(static_cast<TContext&>(*this));
	}

	template <typename TFunc>
	decltype(auto) WithContext(TFunc&& Func) const
	{
		std::lock_guard<std::mutex> Lock(ContextMutex);
		return std::forward<TFunc>(Func)(static_cast<const TContext&>(*this));
	}

	/**
	 * Lock + reference for multi-step context edits.
	 * Keep the returned lock alive for the duration of use.
	 */
	struct FContextAccess
	{
		std::unique_lock<std::mutex> Lock;
		TContext& Context;
	};

	struct FConstContextAccess
	{
		std::unique_lock<std::mutex> Lock;
		const TContext& Context;
	};

	[[nodiscard]] FContextAccess AccessContext()
	{
		return FContextAccess{std::unique_lock<std::mutex>(ContextMutex), static_cast<TContext&>(*this)};
	}

	[[nodiscard]] FConstContextAccess AccessContext() const
	{
		return FConstContextAccess{std::unique_lock<std::mutex>(ContextMutex), static_cast<const TContext&>(*this)};
	}

	// --- Sequencer slots A..J -----------------------------------------------

	[[nodiscard]] TSequencer<A>& GetA() requires (!std::is_void_v<A>)
	{
		return SlotA.Get();
	}

	[[nodiscard]] const TSequencer<A>& GetA() const requires (!std::is_void_v<A>)
	{
		return SlotA.Get();
	}

	[[nodiscard]] TSequencer<B>& GetB() requires (!std::is_void_v<B>)
	{
		return SlotB.Get();
	}

	[[nodiscard]] const TSequencer<B>& GetB() const requires (!std::is_void_v<B>)
	{
		return SlotB.Get();
	}

	[[nodiscard]] TSequencer<C>& GetC() requires (!std::is_void_v<C>)
	{
		return SlotC.Get();
	}

	[[nodiscard]] const TSequencer<C>& GetC() const requires (!std::is_void_v<C>)
	{
		return SlotC.Get();
	}

	[[nodiscard]] TSequencer<D>& GetD() requires (!std::is_void_v<D>)
	{
		return SlotD.Get();
	}

	[[nodiscard]] const TSequencer<D>& GetD() const requires (!std::is_void_v<D>)
	{
		return SlotD.Get();
	}

	[[nodiscard]] TSequencer<E>& GetE() requires (!std::is_void_v<E>)
	{
		return SlotE.Get();
	}

	[[nodiscard]] const TSequencer<E>& GetE() const requires (!std::is_void_v<E>)
	{
		return SlotE.Get();
	}

	[[nodiscard]] TSequencer<F>& GetF() requires (!std::is_void_v<F>)
	{
		return SlotF.Get();
	}

	[[nodiscard]] const TSequencer<F>& GetF() const requires (!std::is_void_v<F>)
	{
		return SlotF.Get();
	}

	[[nodiscard]] TSequencer<G>& GetG() requires (!std::is_void_v<G>)
	{
		return SlotG.Get();
	}

	[[nodiscard]] const TSequencer<G>& GetG() const requires (!std::is_void_v<G>)
	{
		return SlotG.Get();
	}

	[[nodiscard]] TSequencer<H>& GetH() requires (!std::is_void_v<H>)
	{
		return SlotH.Get();
	}

	[[nodiscard]] const TSequencer<H>& GetH() const requires (!std::is_void_v<H>)
	{
		return SlotH.Get();
	}

	[[nodiscard]] TSequencer<TI>& GetI() requires (!std::is_void_v<TI>)
	{
		return SlotI.Get();
	}

	[[nodiscard]] const TSequencer<TI>& GetI() const requires (!std::is_void_v<TI>)
	{
		return SlotI.Get();
	}

	[[nodiscard]] TSequencer<TJ>& GetJ() requires (!std::is_void_v<TJ>)
	{
		return SlotJ.Get();
	}

	[[nodiscard]] const TSequencer<TJ>& GetJ() const requires (!std::is_void_v<TJ>)
	{
		return SlotJ.Get();
	}

	/** True if the corresponding slot was instantiated with a Traits type. */
	[[nodiscard]] static constexpr bool HasA()
	{
		return !std::is_void_v<A>;
	}

	[[nodiscard]] static constexpr bool HasB()
	{
		return !std::is_void_v<B>;
	}

	[[nodiscard]] static constexpr bool HasC()
	{
		return !std::is_void_v<C>;
	}

	[[nodiscard]] static constexpr bool HasD()
	{
		return !std::is_void_v<D>;
	}

	[[nodiscard]] static constexpr bool HasE()
	{
		return !std::is_void_v<E>;
	}

	[[nodiscard]] static constexpr bool HasF()
	{
		return !std::is_void_v<F>;
	}

	[[nodiscard]] static constexpr bool HasG()
	{
		return !std::is_void_v<G>;
	}

	[[nodiscard]] static constexpr bool HasH()
	{
		return !std::is_void_v<H>;
	}

	[[nodiscard]] static constexpr bool HasI()
	{
		return !std::is_void_v<TI>;
	}

	[[nodiscard]] static constexpr bool HasJ()
	{
		return !std::is_void_v<TJ>;
	}

	// --- Wiring / lifecycle -------------------------------------------------

	/**
	 * Record pin deps (Src stage pin → Dst stage gate). Applied in Build().
	 * @return false if already Built, or Src/Dst are not slots of this graph.
	 */
	template <typename TSrcTraits, typename TDstTraits>
	[[nodiscard]] bool BindDep(
		TSequencer<TSrcTraits>& Src,
		TSequencer<TDstTraits>& Dst,
		const TSequencerDep<TSrcTraits, TDstTraits>& Dep)
	{
		if (bBuilt)
		{
			return false;
		}

		const std::size_t SrcSlot = FindSlotIndex(&Src);
		const std::size_t DstSlot = FindSlotIndex(&Dst);
		if (SrcSlot >= SlotCount || DstSlot >= SlotCount)
		{
			return false;
		}

		for (const auto& Pair : Dep.FromTo)
		{
			const std::size_t SourcePin = TSrcTraits::StageToIndex(Pair.first);
			const std::size_t TargetGate = TDstTraits::StageToIndex(Pair.second);
			if (SourcePin >= TSrcTraits::NumStages || TargetGate >= TDstTraits::NumStages)
			{
				continue;
			}

			FPendingPinEdge Edge;
			Edge.SrcSlot = SrcSlot;
			Edge.DstSlot = DstSlot;
			Edge.SrcPin = SourcePin;
			Edge.DstGate = TargetGate;
			Edge.Src = &Src;
			Edge.Dst = &Dst;
			PendingEdges.push_back(Edge);
		}
		return true;
	}

	/**
	 * Validate pin-edge DAG (cycle check on (slot, stage) nodes), then wire listeners.
	 * @return false if already Built, has a cycle, or wiring failed.
	 */
	[[nodiscard]] bool Build()
	{
		if (bBuilt || GraphState != ESequenceGraphState::Stopped)
		{
			return false;
		}
		if (HasPinEdgeCycle())
		{
			return false;
		}

		for (const FPendingPinEdge& Edge : PendingEdges)
		{
			if (!Edge.Src || !Edge.Dst)
			{
				return false;
			}
			Edge.Dst->BindGateExternalExpect(Edge.DstGate, 1);
			ISequencerLockstep* Dst = Edge.Dst;
			const std::size_t SourcePin = Edge.SrcPin;
			const std::size_t TargetGate = Edge.DstGate;
			Edge.Src->AddPinRaiseListener([Dst, SourcePin, TargetGate](std::size_t RaisedStageIndex)
			{
				if (RaisedStageIndex == SourcePin)
				{
					Dst->NotifyExternalPin(TargetGate);
				}
			});
		}

		bBuilt = true;
		return true;
	}

	/**
	 * Start all sequencers (gates + worker loops + PreferMainThread loops).
	 * Blocks until RequestWaitForExit (or stop), then joins workers.
	 * @return false if not Built or not Stopped.
	 */
	[[nodiscard]] bool Execute()
	{
		if (!bBuilt || GraphState != ESequenceGraphState::Stopped)
		{
			return false;
		}

		{
			std::lock_guard<std::mutex> Lock(StateMutex);
			GraphState = ESequenceGraphState::Running;
		}
		OpenZeroExpectGatesAll();
		StartWorkerLoopsAll();
		OnWorkersStarted();
		RunMainThreadObjectLoopsAll();
		WaitWhileRunning();

		{
			std::lock_guard<std::mutex> Lock(StateMutex);
			if (GraphState == ESequenceGraphState::Running)
			{
				GraphState = ESequenceGraphState::WaitForExit;
			}
			GraphState = ESequenceGraphState::ShuttingDown;
		}
		RequestStopAll();
		WaitForStopAll();
		{
			std::lock_guard<std::mutex> Lock(StateMutex);
			GraphState = ESequenceGraphState::Stopped;
		}
		return true;
	}

	/** Transition Running → WaitForExit and request sequencers to stop. */
	void RequestWaitForExit()
	{
		{
			std::lock_guard<std::mutex> Lock(StateMutex);
			if (GraphState != ESequenceGraphState::Running)
			{
				return;
			}
			GraphState = ESequenceGraphState::WaitForExit;
		}
		RequestStopAll();
		StateCV.notify_all();
	}

protected:
	/** Hook after StartWorkerLoopsAll (e.g. bootstrap first Attach gate). */
	virtual void OnWorkersStarted()
	{
	}

public:

	void OpenZeroExpectGatesAll()
	{
		SlotA.OpenZeroExpectGates();
		SlotB.OpenZeroExpectGates();
		SlotC.OpenZeroExpectGates();
		SlotD.OpenZeroExpectGates();
		SlotE.OpenZeroExpectGates();
		SlotF.OpenZeroExpectGates();
		SlotG.OpenZeroExpectGates();
		SlotH.OpenZeroExpectGates();
		SlotI.OpenZeroExpectGates();
		SlotJ.OpenZeroExpectGates();
	}

	void StartWorkerLoopsAll()
	{
		SlotA.StartWorkerLoops();
		SlotB.StartWorkerLoops();
		SlotC.StartWorkerLoops();
		SlotD.StartWorkerLoops();
		SlotE.StartWorkerLoops();
		SlotF.StartWorkerLoops();
		SlotG.StartWorkerLoops();
		SlotH.StartWorkerLoops();
		SlotI.StartWorkerLoops();
		SlotJ.StartWorkerLoops();
	}

	void RunMainThreadObjectLoopsAll()
	{
		// PreferMainThread objects across Module/Layer (and peers) must interleave
		// one stage at a time. Calling SlotA.RunMainThreadObjectLoops() then SlotB
		// deadlocks: Module waits for Layer pins while Layer never pumps.
		while (!(SlotA.IsStopRequested()
			&& SlotB.IsStopRequested()
			&& SlotC.IsStopRequested()
			&& SlotD.IsStopRequested()
			&& SlotE.IsStopRequested()
			&& SlotF.IsStopRequested()
			&& SlotG.IsStopRequested()
			&& SlotH.IsStopRequested()
			&& SlotI.IsStopRequested()
			&& SlotJ.IsStopRequested()))
		{
			const bool bProgressed =
				SlotA.TryPumpMainThreadObjectsOnce()
				|| SlotB.TryPumpMainThreadObjectsOnce()
				|| SlotC.TryPumpMainThreadObjectsOnce()
				|| SlotD.TryPumpMainThreadObjectsOnce()
				|| SlotE.TryPumpMainThreadObjectsOnce()
				|| SlotF.TryPumpMainThreadObjectsOnce()
				|| SlotG.TryPumpMainThreadObjectsOnce()
				|| SlotH.TryPumpMainThreadObjectsOnce()
				|| SlotI.TryPumpMainThreadObjectsOnce()
				|| SlotJ.TryPumpMainThreadObjectsOnce();
			if (!bProgressed)
			{
				if constexpr (!std::is_void_v<A>)
				{
					SlotA.WaitForGateNotify(std::chrono::milliseconds(2));
				}
				else if constexpr (!std::is_void_v<B>)
				{
					SlotB.WaitForGateNotify(std::chrono::milliseconds(2));
				}
			}
		}
	}

	void RequestStopAll()
	{
		SlotA.RequestStop();
		SlotB.RequestStop();
		SlotC.RequestStop();
		SlotD.RequestStop();
		SlotE.RequestStop();
		SlotF.RequestStop();
		SlotG.RequestStop();
		SlotH.RequestStop();
		SlotI.RequestStop();
		SlotJ.RequestStop();
	}

	void WaitForStopAll()
	{
		SlotA.WaitForStop();
		SlotB.WaitForStop();
		SlotC.WaitForStop();
		SlotD.WaitForStop();
		SlotE.WaitForStop();
		SlotF.WaitForStop();
		SlotG.WaitForStop();
		SlotH.WaitForStop();
		SlotI.WaitForStop();
		SlotJ.WaitForStop();
	}

private:
	static constexpr std::size_t SlotCount = 10;
	/** Pack (slot, stage) into one node id; stage index must be < StageIndexStride. */
	static constexpr std::size_t StageIndexStride = 256;

	struct FPendingPinEdge
	{
		std::size_t SrcSlot = 0;
		std::size_t DstSlot = 0;
		std::size_t SrcPin = 0;
		std::size_t DstGate = 0;
		ISequencerLockstep* Src = nullptr;
		ISequencerLockstep* Dst = nullptr;
	};

	[[nodiscard]] static constexpr std::size_t MakeNodeId(std::size_t Slot, std::size_t StageIndex)
	{
		return Slot * StageIndexStride + StageIndex;
	}

	[[nodiscard]] std::size_t FindSlotIndex(const ISequencerLockstep* Ptr) const
	{
		if (!Ptr)
		{
			return SlotCount;
		}
		const std::array<const ISequencerLockstep*, SlotCount> Slots = {
			SlotA.AsLockstep(),
			SlotB.AsLockstep(),
			SlotC.AsLockstep(),
			SlotD.AsLockstep(),
			SlotE.AsLockstep(),
			SlotF.AsLockstep(),
			SlotG.AsLockstep(),
			SlotH.AsLockstep(),
			SlotI.AsLockstep(),
			SlotJ.AsLockstep(),
		};
		for (std::size_t Index = 0; Index < SlotCount; ++Index)
		{
			if (Slots[Index] == Ptr)
			{
				return Index;
			}
		}
		return SlotCount;
	}

	/** DFS cycle detection on SrcPin → DstGate pin edges. */
	[[nodiscard]] bool HasPinEdgeCycle() const
	{
		if (PendingEdges.empty())
		{
			return false;
		}

		const std::size_t NodeCount = SlotCount * StageIndexStride;
		std::vector<std::vector<std::size_t>> Adj(NodeCount);
		std::vector<std::size_t> Seeds;
		Seeds.reserve(PendingEdges.size());

		for (const FPendingPinEdge& Edge : PendingEdges)
		{
			if (Edge.SrcPin >= StageIndexStride || Edge.DstGate >= StageIndexStride)
			{
				return true;
			}
			const std::size_t From = MakeNodeId(Edge.SrcSlot, Edge.SrcPin);
			const std::size_t To = MakeNodeId(Edge.DstSlot, Edge.DstGate);
			Adj[From].push_back(To);
			Seeds.push_back(From);
		}

		enum class EVisit : std::uint8_t
		{
			White = 0,
			Gray,
			Black
		};
		std::vector<EVisit> Color(NodeCount, EVisit::White);

		const auto Dfs = [&](auto&& Self, std::size_t Node) -> bool
		{
			Color[Node] = EVisit::Gray;
			for (std::size_t Next : Adj[Node])
			{
				if (Color[Next] == EVisit::Gray)
				{
					return true;
				}
				if (Color[Next] == EVisit::White && Self(Self, Next))
				{
					return true;
				}
			}
			Color[Node] = EVisit::Black;
			return false;
		};

		for (std::size_t Seed : Seeds)
		{
			if (Color[Seed] == EVisit::White && Dfs(Dfs, Seed))
			{
				return true;
			}
		}
		return false;
	}

	void WaitWhileRunning()
	{
		std::unique_lock<std::mutex> Lock(StateMutex);
		StateCV.wait(Lock, [this]()
		{
			return GraphState != ESequenceGraphState::Running;
		});
	}

	mutable std::mutex ContextMutex;
	mutable std::mutex StateMutex;
	std::condition_variable StateCV;
	ESequenceGraphState GraphState = ESequenceGraphState::Stopped;
	bool bBuilt = false;
	std::vector<FPendingPinEdge> PendingEdges;

	Private::TSequenceGraphSlot<A> SlotA;
	Private::TSequenceGraphSlot<B> SlotB;
	Private::TSequenceGraphSlot<C> SlotC;
	Private::TSequenceGraphSlot<D> SlotD;
	Private::TSequenceGraphSlot<E> SlotE;
	Private::TSequenceGraphSlot<F> SlotF;
	Private::TSequenceGraphSlot<G> SlotG;
	Private::TSequenceGraphSlot<H> SlotH;
	Private::TSequenceGraphSlot<TI> SlotI;
	Private::TSequenceGraphSlot<TJ> SlotJ;
};

} // namespace Catty
