#pragma once

#include "Catty/Core/Export.h"
#include "Catty/Resource/Object.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace Catty
{

/**
 * Type-agnostic game-thread GC (refcount scan, no reference-graph walk).
 * Owners allocate from their own pools, RegisterObject, and provide destroy handlers.
 *
 * An object is live if RefCount > 0 (including one FObjectRef per rooted object in GC).
 * AddToRoot inserts into a deduped root map (nested calls bump NestCount only).
 * Collectible when unreferenced AND:
 *   - FPackage itself, or
 *   - any in-package FObject (Outer pin / catalog FObjectRef keep packages alive while needed).
 * Mutual FObjectRef cycles are never collected — break cycles with FObjectWeakRef.
 *
 * Example:
 * ```
 *   GCManager.Initialize();
 *   const auto HandlerId = GCManager.AddObjectDestroyHandler(
 *       [&](FObject* Obj) { return TryDestroyMyType(Obj); });
 *   FMyObject* Obj = MyPool.Allocate(...);
 *   GCManager.RegisterObject(*Obj);
 *   Obj->AddToRoot();
 *   GCManager.Tick(DeltaSeconds);
 *   GCManager.RemoveObjectDestroyHandler(HandlerId);
 * ```
 */
class CATTY_API FGCManager
{
public:
	/** @return true if this handler destroyed / claimed the object. */
	using FObjectDestroyHandler = std::function<bool(FObject*)>;
	using FObjectDestroyHandlerId = std::uint64_t;

	static constexpr FObjectDestroyHandlerId InvalidDestroyHandlerId = 0;

	FGCManager() = default;
	~FGCManager();

	FGCManager(const FGCManager&) = delete;
	FGCManager& operator=(const FGCManager&) = delete;

	[[nodiscard]] bool Initialize();
	void Shutdown();

	[[nodiscard]] bool IsInitialized() const { return bInitialized; }

	/**
	 * Register a typed destroyer (ResourceManager, future AudioManager, ...).
	 * Handlers are tried in order until one returns true.
	 * @return Id for RemoveObjectDestroyHandler (0 on empty handler).
	 */
	FObjectDestroyHandlerId AddObjectDestroyHandler(FObjectDestroyHandler Handler);
	bool RemoveObjectDestroyHandler(FObjectDestroyHandlerId Id);
	void ClearObjectDestroyHandlers();

	/** Track a newly allocated FObject (sets GCOwner). Does not allocate memory. */
	void RegisterObject(FObject& Object);

	/** Drop from Live/Root/Pending lists only — caller still owns / must Free the pool slot. */
	void UnregisterObject(FObject& Object);

	/**
	 * Run destroy handlers now (owner Frees pool memory).
	 * Used by Unload and by Immediate/Pending purge.
	 */
	void DestroyObjectImmediate(FObject* Object);

	void AddToRoot(FObject& Object);
	void RemoveFromRoot(FObject& Object);

	/**
	 * Enqueue from MarkPendingKill / MarkForImmediateDestroy.
	 * If still RefCount>0 the entry stays until Collect sees it dead.
	 */
	void EnqueuePendingKill(FObject& Object);
	void EnqueueImmediateDestroy(FObject& Object);

	/**
	 * Scan unreferenced eligible objects → PendingKill / Immediate queues,
	 * then Free anything already in the Immediate queue.
	 */
	void CollectGarbage();
	void PurgePendingKill();
	void Tick(float DeltaSeconds);

	void SetPurgeIntervalSeconds(float Seconds) { PurgeIntervalSeconds = Seconds; }
	[[nodiscard]] float GetPurgeIntervalSeconds() const { return PurgeIntervalSeconds; }

	void SetCollectIntervalSeconds(float Seconds) { CollectIntervalSeconds = Seconds; }
	[[nodiscard]] float GetCollectIntervalSeconds() const { return CollectIntervalSeconds; }

	[[nodiscard]] std::size_t GetLiveObjectCount() const { return LiveObjects.size(); }
	/** Number of uniquely rooted objects. */
	[[nodiscard]] std::size_t GetRootCount() const { return RootMap.size(); }
	[[nodiscard]] std::size_t GetPendingKillCount() const { return PendingKill.size(); }

	[[nodiscard]] bool IsInRootSet(const FObject& Object) const;

private:
	struct FRootEntry
	{
		FObjectRef Ref;
		std::uint32_t NestCount = 0;
	};

	[[nodiscard]] static bool IsKeptAlive(const FObject& Object);
	/** True if unreferenced object may enter PendingKill / Immediate queues. */
	[[nodiscard]] static bool IsGcEligible(const FObject& Object);

	void QueueUnreferenced();
	void ProcessImmediateDestroy();
	void RemoveAllRootRefs(FObject* Object);
	void RemoveFromPendingKill(FObject* Object);
	void RemoveFromImmediate(FObject* Object);

	struct FDestroyHandlerEntry
	{
		FObjectDestroyHandlerId Id = InvalidDestroyHandlerId;
		FObjectDestroyHandler Handler;
	};

	bool bInitialized = false;
	FObjectDestroyHandlerId NextDestroyHandlerId = 1;
	std::vector<FDestroyHandlerEntry> DestroyHandlers;

	std::vector<FObject*> LiveObjects;
	/** One strong Ref per rooted object; NestCount supports nested AddToRoot. */
	std::unordered_map<FObject*, FRootEntry> RootMap;
	std::vector<FObject*> PendingKill;
	std::vector<FObject*> ImmediateDestroy;

	float CollectIntervalSeconds = 1.0f;
	float CollectAccumulatorSeconds = 0.0f;
	float PurgeIntervalSeconds = 30.0f;
	float PurgeAccumulatorSeconds = 0.0f;
};

} // namespace Catty
