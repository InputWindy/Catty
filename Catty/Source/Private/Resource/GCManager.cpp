#include "Catty/Resource/GCManager.h"

#include "Catty/Core/Log.h"
#include "Catty/Resource/Package.h"

#include <algorithm>

namespace Catty
{

void FGCManager::FMarkCollector::AddReferencedObject(FObject*& Object)
{
	if (!Object)
	{
		return;
	}

	if (Object->HasAnyFlags(EObjectFlags::Reachable))
	{
		return;
	}

	Object->AddFlags(EObjectFlags::Reachable);
	Queue.push_back(Object);
}

FGCManager::~FGCManager()
{
	Shutdown();
}

bool FGCManager::Initialize()
{
	if (bInitialized)
	{
		return true;
	}

	bInitialized = true;
	CATTY_CORE_INFO("GCManager initialized");
	return true;
}

void FGCManager::Shutdown()
{
	if (!bInitialized)
	{
		return;
	}

	if (!LiveObjects.empty())
	{
		CATTY_CORE_WARN(
			"GCManager::Shutdown: {} live object(s) remain — owners should destroy them first",
			LiveObjects.size());

		std::vector<FObject*> Snapshot = LiveObjects;
		for (FObject* Object : Snapshot)
		{
			DestroyObjectImmediate(Object);
		}
	}

	LiveObjects.clear();
	RootSet.clear();
	MarkSeeds.clear();
	PendingKill.clear();
	ImmediateDestroy.clear();
	DestroyHandlers.clear();
	bInitialized = false;
	CATTY_CORE_INFO("GCManager shut down");
}

void FGCManager::AddObjectDestroyHandler(FObjectDestroyHandler Handler)
{
	if (Handler)
	{
		DestroyHandlers.push_back(std::move(Handler));
	}
}

void FGCManager::ClearObjectDestroyHandlers()
{
	DestroyHandlers.clear();
}

void FGCManager::RegisterObject(FObject& Object)
{
	if (!bInitialized)
	{
		CATTY_CORE_ERROR("FGCManager::RegisterObject: not initialized");
		return;
	}

	Object.GCOwner = this;
	LiveObjects.push_back(&Object);
}

void FGCManager::UnregisterObject(FObject& Object)
{
	LiveObjects.erase(
		std::remove(LiveObjects.begin(), LiveObjects.end(), &Object),
		LiveObjects.end());
	RemoveFromRootSet(&Object);
	RemoveFromPendingKill(&Object);
	RemoveFromImmediate(&Object);
	Object.GCOwner = nullptr;
}

void FGCManager::RemoveFromRootSet(FObject* Object)
{
	RootSet.erase(std::remove(RootSet.begin(), RootSet.end(), Object), RootSet.end());
}

void FGCManager::RemoveFromPendingKill(FObject* Object)
{
	PendingKill.erase(std::remove(PendingKill.begin(), PendingKill.end(), Object), PendingKill.end());
}

void FGCManager::RemoveFromImmediate(FObject* Object)
{
	ImmediateDestroy.erase(
		std::remove(ImmediateDestroy.begin(), ImmediateDestroy.end(), Object),
		ImmediateDestroy.end());
}

bool FGCManager::IsInTransientPackage(const FObject& Object)
{
	const FPackageRef Package = Object.GetOuter();
	return Package && Package->IsTransient();
}

void FGCManager::DestroyObjectImmediate(FObject* Object)
{
	if (!Object)
	{
		return;
	}

	for (const FObjectDestroyHandler& Handler : DestroyHandlers)
	{
		if (Handler && Handler(Object))
		{
			return;
		}
	}

	CATTY_CORE_ERROR(
		"FGCManager::DestroyObjectImmediate: no destroy handler claimed '{}'",
		Object->GetPathName());
	UnregisterObject(*Object);
}

void FGCManager::AddToRoot(FObject& Object)
{
	const bool bWasRooted = Object.IsRooted();
	++Object.RootCount;
	if (!bWasRooted)
	{
		RootSet.push_back(&Object);
	}

	if (Object.IsPendingKill())
	{
		Object.ClearFlags(EObjectFlags::PendingKill);
		RemoveFromPendingKill(&Object);
	}
}

void FGCManager::RemoveFromRoot(FObject& Object)
{
	if (Object.RootCount == 0)
	{
		CATTY_CORE_WARN("FGCManager::RemoveFromRoot: '{}' already has RootCount==0", Object.GetPathName());
		return;
	}

	--Object.RootCount;
	if (!Object.IsRooted())
	{
		RemoveFromRootSet(&Object);
	}
}

void FGCManager::AddMarkSeed(FObject* Object)
{
	if (Object)
	{
		MarkSeeds.push_back(Object);
	}
}

void FGCManager::ClearReachableFlags()
{
	for (FObject* Object : LiveObjects)
	{
		if (Object)
		{
			Object->ClearFlags(EObjectFlags::Reachable);
		}
	}
}

void FGCManager::MarkReachable()
{
	std::vector<FObject*> Queue;
	Queue.reserve(LiveObjects.size());

	auto EnqueueSeed = [&Queue](FObject* Object)
	{
		if (!Object || Object->HasAnyFlags(EObjectFlags::Reachable))
		{
			return;
		}
		Object->AddFlags(EObjectFlags::Reachable);
		Queue.push_back(Object);
	};

	for (FObject* Object : RootSet)
	{
		EnqueueSeed(Object);
	}

	for (FObject* Object : MarkSeeds)
	{
		EnqueueSeed(Object);
	}
	MarkSeeds.clear();

	for (FObject* Object : LiveObjects)
	{
		if (Object && Object->GetRefCount() > 0)
		{
			EnqueueSeed(Object);
		}
	}

	FMarkCollector Collector(Queue);
	std::size_t Index = 0;
	while (Index < Queue.size())
	{
		FObject* Object = Queue[Index++];
		if (!Object)
		{
			continue;
		}
		Object->AddReferencedObjects(Collector);
	}
}

void FGCManager::QueueUnreachable()
{
	for (FObject* Object : LiveObjects)
	{
		if (!Object)
		{
			continue;
		}

		if (Object->IsReachable() || Object->IsRooted())
		{
			if (Object->IsPendingKill())
			{
				Object->ClearFlags(EObjectFlags::PendingKill);
				RemoveFromPendingKill(Object);
			}
			if (Object->HasAnyFlags(EObjectFlags::ImmediateDestroy))
			{
				Object->ClearFlags(EObjectFlags::ImmediateDestroy);
				RemoveFromImmediate(Object);
			}
			continue;
		}

		if (!IsInTransientPackage(*Object))
		{
			continue;
		}

		if (Object->HasAnyFlags(EObjectFlags::ImmediateDestroy))
		{
			if (std::find(ImmediateDestroy.begin(), ImmediateDestroy.end(), Object) == ImmediateDestroy.end())
			{
				ImmediateDestroy.push_back(Object);
			}
			continue;
		}

		if (!Object->IsPendingKill())
		{
			Object->MarkPendingKill();
		}

		if (std::find(PendingKill.begin(), PendingKill.end(), Object) == PendingKill.end())
		{
			PendingKill.push_back(Object);
		}
	}
}

void FGCManager::CollectGarbage()
{
	if (!bInitialized)
	{
		return;
	}

	ClearReachableFlags();
	MarkReachable();
	QueueUnreachable();
}

void FGCManager::ProcessImmediateDestroy()
{
	if (ImmediateDestroy.empty())
	{
		return;
	}

	std::vector<FObject*> ToFree = std::move(ImmediateDestroy);
	ImmediateDestroy.clear();

	for (FObject* Object : ToFree)
	{
		if (!Object)
		{
			continue;
		}

		if (Object->IsReachable() || Object->IsRooted() || Object->GetRefCount() > 0)
		{
			Object->ClearFlags(EObjectFlags::ImmediateDestroy);
			continue;
		}

		CATTY_CORE_INFO("GC immediate destroy: {}", Object->GetPathName());
		DestroyObjectImmediate(Object);
	}
}

void FGCManager::PurgePendingKill()
{
	if (!bInitialized || PendingKill.empty())
	{
		return;
	}

	ClearReachableFlags();
	MarkReachable();

	std::vector<FObject*> ToFree;
	ToFree.reserve(PendingKill.size());

	for (FObject* Object : PendingKill)
	{
		if (!Object)
		{
			continue;
		}

		if (Object->IsReachable() || Object->IsRooted() || Object->GetRefCount() > 0)
		{
			Object->ClearFlags(EObjectFlags::PendingKill);
			continue;
		}

		if (!IsInTransientPackage(*Object))
		{
			Object->ClearFlags(EObjectFlags::PendingKill);
			continue;
		}

		ToFree.push_back(Object);
	}

	PendingKill.clear();

	for (FObject* Object : ToFree)
	{
		CATTY_CORE_INFO("GC purge pending kill: {}", Object->GetPathName());
		DestroyObjectImmediate(Object);
	}
}

void FGCManager::Tick(float DeltaSeconds)
{
	if (!bInitialized)
	{
		return;
	}

	ProcessImmediateDestroy();

	CollectAccumulatorSeconds += DeltaSeconds;
	if (CollectIntervalSeconds <= 0.0f || CollectAccumulatorSeconds >= CollectIntervalSeconds)
	{
		CollectAccumulatorSeconds = 0.0f;
		CollectGarbage();
		ProcessImmediateDestroy();
	}

	PurgeAccumulatorSeconds += DeltaSeconds;
	if (PurgeIntervalSeconds <= 0.0f || PurgeAccumulatorSeconds >= PurgeIntervalSeconds)
	{
		PurgeAccumulatorSeconds = 0.0f;
		PurgePendingKill();
	}
}

} // namespace Catty
