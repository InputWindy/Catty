#pragma once

#include <Core/Export.h>
#include <Core/Object.h>

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
 * Live if RefCount > 0 (including one FObjectRef per rooted object in RootMap).
 * AddToRoot is nested (NestCount); game code uses FObject::AddToRoot / Mark*.
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

	// --- Lifecycle ---
	[[nodiscard]] bool Initialize();
	void Shutdown();
	[[nodiscard]] bool IsInitialized() const { return bInitialized; }

	// --- Owner registration (ResourceManager, future AudioManager, ...) ---
	/**
	 * Register a typed destroyer. Handlers are tried in order until one returns true.
	 * @return Id for RemoveObjectDestroyHandler (0 on empty handler).
	 */
	FObjectDestroyHandlerId AddObjectDestroyHandler(FObjectDestroyHandler Handler);
	bool RemoveObjectDestroyHandler(FObjectDestroyHandlerId Id);

	/** Track a newly allocated FObject (sets GCOwner). Does not allocate memory. */
	void RegisterObject(FObject& Object);

	/** Drop from Live/Root/Pending lists only — caller still owns / must Free the pool slot. */
	void UnregisterObject(FObject& Object);

	/**
	 * Run destroy handlers now (owner Frees pool memory).
	 * Used by Immediate/Pending purge and owner teardown helpers.
	 */
	void DestroyObjectImmediate(FObject* Object);

	// --- Collection ---
	/**
	 * Scan unreferenced objects → PendingKill / Immediate queues,
	 * then Free anything already in the Immediate queue.
	 */
	void CollectGarbage();
	void PurgePendingKill();
	void Tick(float DeltaSeconds);

	// --- Timing ---
	void SetPurgeIntervalSeconds(float Seconds) { PurgeIntervalSeconds = Seconds; }
	void SetCollectIntervalSeconds(float Seconds) { CollectIntervalSeconds = Seconds; }

private:
	friend class FObject;

	struct FRootEntry
	{
		FObjectRef Ref;
		std::uint32_t NestCount = 0;
	};

	// --- FObject-facing (use FObject::AddToRoot / Mark* from game code) ---
	void AddToRoot(FObject& Object);
	void RemoveFromRoot(FObject& Object);
	void EnqueuePendingKill(FObject& Object);
	void EnqueueImmediateDestroy(FObject& Object);
	[[nodiscard]] bool IsInRootSet(const FObject& Object) const;

	// --- Internals ---
	[[nodiscard]] static bool IsKeptAlive(const FObject& Object);

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
