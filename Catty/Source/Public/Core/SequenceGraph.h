#pragma once

/**
 * TSequenceGraph: owns N typed TSequencer slots (variadic Traits pack) plus one
 * cross-sequencer thread-safe TContext (first template parameter, CRTP).
 *
 * Example:
 * ```
 *   using FGraph = Catty::TSequenceGraph<FMyApp, FModuleTraits, FLayerTraits>;
 *   Graph.WithContext([](FMyApp& App) { App.DoWork(); });
 *   Graph.GetA().Register(Module);   // Get<0>()
 *   Graph.GetB().Register(Layer);    // Get<1>()
 *   Graph.BindDep(Graph.GetA(), Graph.GetB(), Dep);
 *   if (!Graph.Build()) { ... }
 *   Graph.Execute();
 * ```
 */

#include <Core/Sequencer.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <tuple>
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

/**
 * Graph of sequencers + shared cross-sequence context.
 *
 * Template parameters:
 *   TContext      — CRTP context (WithContext / AccessContext)
 *   TSlotTraits... — one Traits type per sequencer slot (void not allowed)
 */
template <typename TContext, typename... TSlotTraits>
class TSequenceGraph
{
	static_assert(sizeof...(TSlotTraits) >= 1, "TSequenceGraph needs at least one slot Traits");
	static_assert((!std::is_void_v<TSlotTraits> && ...), "void slot Traits are not allowed");

public:
	using FContext = TContext;
	static constexpr std::size_t SlotCount = sizeof...(TSlotTraits);

	template <std::size_t I>
	using FTraitsAt = std::tuple_element_t<I, std::tuple<TSlotTraits...>>;

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

	// --- Sequencer slots ----------------------------------------------------

	template <std::size_t I>
	[[nodiscard]] TSequencer<FTraitsAt<I>>& Get()
	{
		static_assert(I < SlotCount, "Get<I>: slot index out of range");
		return std::get<I>(Sequencers);
	}

	template <std::size_t I>
	[[nodiscard]] const TSequencer<FTraitsAt<I>>& Get() const
	{
		static_assert(I < SlotCount, "Get<I>: slot index out of range");
		return std::get<I>(Sequencers);
	}

	/** Compatibility aliases for the first two slots (Module / Layer). */
	[[nodiscard]] TSequencer<FTraitsAt<0>>& GetA()
	{
		return Get<0>();
	}

	[[nodiscard]] const TSequencer<FTraitsAt<0>>& GetA() const
	{
		return Get<0>();
	}

	[[nodiscard]] TSequencer<FTraitsAt<1>>& GetB() requires (SlotCount > 1)
	{
		return Get<1>();
	}

	[[nodiscard]] const TSequencer<FTraitsAt<1>>& GetB() const requires (SlotCount > 1)
	{
		return Get<1>();
	}

	template <std::size_t I>
	[[nodiscard]] static constexpr bool HasSlot()
	{
		return I < SlotCount;
	}

	[[nodiscard]] static constexpr bool HasA()
	{
		return HasSlot<0>();
	}

	[[nodiscard]] static constexpr bool HasB()
	{
		return HasSlot<1>();
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
	/**
	 * Subclass fills BindDep edges (and any extra pin listeners), then typically calls Build().
	 * Default: Build() with no extra edges.
	 */
	[[nodiscard]] virtual bool BuildGraph()
	{
		return Build();
	}

	/** Hook after StartWorkerLoopsAll (e.g. bootstrap first Attach gate). */
	virtual void OnWorkersStarted()
	{
	}

public:

	void OpenZeroExpectGatesAll()
	{
		ForEachSequencer([](auto& Seq)
		{
			Seq.OpenZeroExpectGates();
		});
	}

	void StartWorkerLoopsAll()
	{
		ForEachSequencer([](auto& Seq)
		{
			Seq.StartWorkerLoops();
		});
	}

	void RunMainThreadObjectLoopsAll()
	{
		// PreferMainThread objects across slots must interleave one stage at a time.
		while (!AreAllStopRequested())
		{
			if (!TryPumpMainThreadObjectsOnceAll())
			{
				Get<0>().WaitForGateNotify(std::chrono::milliseconds(2));
			}
		}
	}

	void RequestStopAll()
	{
		ForEachSequencer([](auto& Seq)
		{
			Seq.RequestStop();
		});
	}

	void WaitForStopAll()
	{
		ForEachSequencer([](auto& Seq)
		{
			Seq.WaitForStop();
		});
	}

private:
	/** Pack (slot, stage) into one node id; stage index must be < StageIndexStride. */
	static constexpr std::size_t StageIndexStride = 256;

	using FSequencerTuple = std::tuple<TSequencer<TSlotTraits>...>;

	struct FPendingPinEdge
	{
		std::size_t SrcSlot = 0;
		std::size_t DstSlot = 0;
		std::size_t SrcPin = 0;
		std::size_t DstGate = 0;
		ISequencerLockstep* Src = nullptr;
		ISequencerLockstep* Dst = nullptr;
	};

	template <typename TFunc>
	void ForEachSequencer(TFunc&& Func)
	{
		ForEachSequencerImpl(std::forward<TFunc>(Func), std::make_index_sequence<SlotCount>{});
	}

	template <typename TFunc>
	void ForEachSequencer(TFunc&& Func) const
	{
		ForEachSequencerImpl(std::forward<TFunc>(Func), std::make_index_sequence<SlotCount>{});
	}

	template <typename TFunc, std::size_t... Is>
	void ForEachSequencerImpl(TFunc&& Func, std::index_sequence<Is...>)
	{
		(Func(std::get<Is>(Sequencers)), ...);
	}

	template <typename TFunc, std::size_t... Is>
	void ForEachSequencerImpl(TFunc&& Func, std::index_sequence<Is...>) const
	{
		(Func(std::get<Is>(Sequencers)), ...);
	}

	[[nodiscard]] bool AreAllStopRequested() const
	{
		return AreAllStopRequestedImpl(std::make_index_sequence<SlotCount>{});
	}

	template <std::size_t... Is>
	[[nodiscard]] bool AreAllStopRequestedImpl(std::index_sequence<Is...>) const
	{
		return (std::get<Is>(Sequencers).IsStopRequested() && ...);
	}

	[[nodiscard]] bool TryPumpMainThreadObjectsOnceAll()
	{
		return TryPumpMainThreadObjectsOnceAllImpl(std::make_index_sequence<SlotCount>{});
	}

	template <std::size_t... Is>
	[[nodiscard]] bool TryPumpMainThreadObjectsOnceAllImpl(std::index_sequence<Is...>)
	{
		return (std::get<Is>(Sequencers).TryPumpMainThreadObjectsOnce() || ...);
	}

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
		return FindSlotIndexImpl(Ptr, std::make_index_sequence<SlotCount>{});
	}

	template <std::size_t... Is>
	[[nodiscard]] std::size_t FindSlotIndexImpl(
		const ISequencerLockstep* Ptr,
		std::index_sequence<Is...>) const
	{
		std::size_t Found = SlotCount;
		((&std::get<Is>(Sequencers) == Ptr ? (Found = Is, true) : false) || ...);
		return Found;
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
	FSequencerTuple Sequencers{};
};

} // namespace Catty
