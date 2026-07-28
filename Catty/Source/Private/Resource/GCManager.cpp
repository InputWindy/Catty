#include "Resource/GCManager.h"

#include "Core/ConsoleManager.h"
#include "Core/Log.h"

#include <algorithm>

namespace Catty
{

namespace
{

static TAutoConsoleVariable GCVarCollectInterval(
	"gc.CollectInterval",
	1.0f,
	"Seconds between CollectGarbage scans (0 = every Tick)");

static TAutoConsoleVariable GCVarPurgeInterval(
	"gc.PurgeInterval",
	30.0f,
	"Seconds between PurgePendingKill (0 = every Tick)");

} // namespace

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

	CollectIntervalSeconds = GCVarCollectInterval.GetValue();
	PurgeIntervalSeconds = GCVarPurgeInterval.GetValue();
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
	RootMap.clear();
	PendingKill.clear();
	ImmediateDestroy.clear();
	DestroyHandlers.clear();
	bInitialized = false;
	CATTY_CORE_INFO("GCManager shut down");
}

FGCManager::FObjectDestroyHandlerId FGCManager::AddObjectDestroyHandler(FObjectDestroyHandler Handler)
{
	if (!Handler)
	{
		return InvalidDestroyHandlerId;
	}

	const FObjectDestroyHandlerId Id = NextDestroyHandlerId++;
	if (NextDestroyHandlerId == InvalidDestroyHandlerId)
	{
		NextDestroyHandlerId = 1;
	}

	DestroyHandlers.push_back(FDestroyHandlerEntry{Id, std::move(Handler)});
	return Id;
}

bool FGCManager::RemoveObjectDestroyHandler(FObjectDestroyHandlerId Id)
{
	if (Id == InvalidDestroyHandlerId)
	{
		return false;
	}

	const auto It = std::remove_if(
		DestroyHandlers.begin(),
		DestroyHandlers.end(),
		[Id](const FDestroyHandlerEntry& Entry) { return Entry.Id == Id; });
	if (It == DestroyHandlers.end())
	{
		return false;
	}

	DestroyHandlers.erase(It, DestroyHandlers.end());
	return true;
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
	RemoveAllRootRefs(&Object);
	RemoveFromPendingKill(&Object);
	RemoveFromImmediate(&Object);
	Object.GCOwner = nullptr;
}

void FGCManager::RemoveAllRootRefs(FObject* Object)
{
	if (!Object)
	{
		return;
	}

	RootMap.erase(Object);
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

bool FGCManager::IsKeptAlive(const FObject& Object)
{
	return Object.GetRefCount() > 0;
}

bool FGCManager::IsInRootSet(const FObject& Object) const
{
	return RootMap.find(const_cast<FObject*>(&Object)) != RootMap.end();
}

void FGCManager::DestroyObjectImmediate(FObject* Object)
{
	if (!Object)
	{
		return;
	}

	for (const FDestroyHandlerEntry& Entry : DestroyHandlers)
	{
		if (Entry.Handler && Entry.Handler(Object))
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
	const auto It = RootMap.find(&Object);
	if (It != RootMap.end())
	{
		++It->second.NestCount;
	}
	else
	{
		FRootEntry Entry;
		Entry.Ref = FObjectRef::Wrap(&Object);
		Entry.NestCount = 1;
		RootMap.emplace(&Object, std::move(Entry));
	}

	if (Object.IsPendingKill())
	{
		Object.ClearFlags(EObjectFlags::PendingKill);
		RemoveFromPendingKill(&Object);
	}
	if (Object.HasAnyFlags(EObjectFlags::ImmediateDestroy))
	{
		Object.ClearFlags(EObjectFlags::ImmediateDestroy);
		RemoveFromImmediate(&Object);
	}
}

void FGCManager::RemoveFromRoot(FObject& Object)
{
	const auto It = RootMap.find(&Object);
	if (It == RootMap.end())
	{
		CATTY_CORE_WARN("FGCManager::RemoveFromRoot: '{}' is not in the root map", Object.GetPathName());
		return;
	}

	if (It->second.NestCount > 1)
	{
		--It->second.NestCount;
		return;
	}

	RootMap.erase(It);
}

void FGCManager::EnqueuePendingKill(FObject& Object)
{
	Object.ClearFlags(EObjectFlags::ImmediateDestroy);
	Object.AddFlags(EObjectFlags::PendingKill);
	RemoveFromImmediate(&Object);

	if (std::find(PendingKill.begin(), PendingKill.end(), &Object) == PendingKill.end())
	{
		PendingKill.push_back(&Object);
	}
}

void FGCManager::EnqueueImmediateDestroy(FObject& Object)
{
	Object.ClearFlags(EObjectFlags::PendingKill);
	Object.AddFlags(EObjectFlags::ImmediateDestroy);
	RemoveFromPendingKill(&Object);

	if (std::find(ImmediateDestroy.begin(), ImmediateDestroy.end(), &Object) == ImmediateDestroy.end())
	{
		ImmediateDestroy.push_back(&Object);
	}
}

void FGCManager::QueueUnreferenced()
{
	for (FObject* Object : LiveObjects)
	{
		if (!Object)
		{
			continue;
		}

		if (IsKeptAlive(*Object))
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
			Object->AddFlags(EObjectFlags::PendingKill);
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

	QueueUnreferenced();
	ProcessImmediateDestroy();
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

		if (IsKeptAlive(*Object))
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

	std::vector<FObject*> ToFree;
	ToFree.reserve(PendingKill.size());

	for (FObject* Object : PendingKill)
	{
		if (!Object)
		{
			continue;
		}

		if (IsKeptAlive(*Object))
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

	// Allow runtime ini/console changes to take effect.
	CollectIntervalSeconds = GCVarCollectInterval.GetValue();
	PurgeIntervalSeconds = GCVarPurgeInterval.GetValue();

	ProcessImmediateDestroy();

	CollectAccumulatorSeconds += DeltaSeconds;
	if (CollectIntervalSeconds <= 0.0f || CollectAccumulatorSeconds >= CollectIntervalSeconds)
	{
		CollectAccumulatorSeconds = 0.0f;
		CollectGarbage();
	}

	PurgeAccumulatorSeconds += DeltaSeconds;
	if (PurgeIntervalSeconds <= 0.0f || PurgeAccumulatorSeconds >= PurgeIntervalSeconds)
	{
		PurgeAccumulatorSeconds = 0.0f;
		PurgePendingKill();
	}
}

} // namespace Catty
