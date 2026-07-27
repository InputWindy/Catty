#pragma once

#include "Catty/Core/Export.h"
#include "Catty/Resource/Object.h"
#include "Catty/Resource/ReferenceCollector.h"

#include <functional>
#include <vector>

namespace Catty
{

/**
 * Type-agnostic game-thread object GC (UE GC lite).
 * Does NOT allocate concrete FObject subclasses — owners (e.g. FResourceManager)
 * allocate from their own TPoolAllocator, then RegisterObject / provide a destroy handler.
 *
 * GC owns: LiveObjects registry, RootSet, Mark (AddReferencedObjects graph),
 * PendingKill / ImmediateDestroy queues, timed Purge.
 *
 * Example:
 * ```
 *   GCManager.Initialize();
 *   GCManager.AddObjectDestroyHandler([&](FObject* Obj) { return TryDestroyMyType(Obj); });
 *   FMyObject* Obj = MyPool.Allocate(...);
 *   GCManager.RegisterObject(*Obj);
 *   Obj->AddToRoot();
 *   GCManager.Tick(DeltaSeconds);
 * ```
 */
class CATTY_API FGCManager
{
public:
	/** @return true if this handler destroyed / claimed the object. */
	using FObjectDestroyHandler = std::function<bool(FObject*)>;

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
	 */
	void AddObjectDestroyHandler(FObjectDestroyHandler Handler);
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

	void AddMarkSeed(FObject* Object);

	void CollectGarbage();
	void PurgePendingKill();
	void Tick(float DeltaSeconds);

	void SetPurgeIntervalSeconds(float Seconds) { PurgeIntervalSeconds = Seconds; }
	[[nodiscard]] float GetPurgeIntervalSeconds() const { return PurgeIntervalSeconds; }

	void SetCollectIntervalSeconds(float Seconds) { CollectIntervalSeconds = Seconds; }
	[[nodiscard]] float GetCollectIntervalSeconds() const { return CollectIntervalSeconds; }

	[[nodiscard]] std::size_t GetLiveObjectCount() const { return LiveObjects.size(); }
	[[nodiscard]] std::size_t GetRootCount() const { return RootSet.size(); }
	[[nodiscard]] std::size_t GetPendingKillCount() const { return PendingKill.size(); }

private:
	class FMarkCollector final : public FReferenceCollector
	{
	public:
		explicit FMarkCollector(std::vector<FObject*>& InQueue)
			: Queue(InQueue)
		{
		}

		void AddReferencedObject(FObject*& Object) override;

	private:
		std::vector<FObject*>& Queue;
	};

	void ClearReachableFlags();
	void MarkReachable();
	void QueueUnreachable();
	void ProcessImmediateDestroy();
	void RemoveFromRootSet(FObject* Object);
	void RemoveFromPendingKill(FObject* Object);
	void RemoveFromImmediate(FObject* Object);

	[[nodiscard]] static bool IsTransientExport(const FObject& Object);

	bool bInitialized = false;
	std::vector<FObjectDestroyHandler> DestroyHandlers;

	std::vector<FObject*> LiveObjects;
	std::vector<FObject*> RootSet;
	std::vector<FObject*> MarkSeeds;
	std::vector<FObject*> PendingKill;
	std::vector<FObject*> ImmediateDestroy;

	float CollectIntervalSeconds = 1.0f;
	float CollectAccumulatorSeconds = 0.0f;
	float PurgeIntervalSeconds = 30.0f;
	float PurgeAccumulatorSeconds = 0.0f;
};

} // namespace Catty
