#include <Core/GC.h>

#include <Core/ConsoleManager.h>
#include <Core/App.h>
#include <Core/Log.h>
#include <Core/Modules/GCModule.h>
#include <Core/Paths.h>
#include <Core/Resource/Package.h>
#include <ObjectReflectTypes.gen.h>

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

FGC::~FGC()
{
	Shutdown();
}

bool FGC::Initialize()
{
	if (bInitialized)
	{
		return true;
	}

	CollectIntervalSeconds = GCVarCollectInterval.GetValue();
	PurgeIntervalSeconds = GCVarPurgeInterval.GetValue();
	bInitialized = true;
	RegisterGeneratedGCPooledTypes(*this);
	CATTY_CORE_INFO("GC initialized");
	return true;
}

void FGC::Shutdown()
{
	if (!bInitialized)
	{
		return;
	}

	// Drop root pins so unreferenced objects can reach PendingKill.
	RootMap.clear();

	CollectGarbage();
	PurgePendingKill();

	if (!LiveObjects.empty())
	{
		CATTY_CORE_WARN(
			"FGC::Shutdown: {} live object(s) remain — holders still own FObjectRef(s)",
			LiveObjects.size());
	}

	LiveObjects.clear();
	PendingKill.clear();
	PooledTypes.clear();
	bInitialized = false;
	CATTY_CORE_INFO("GC shut down");
}

void FGC::RegisterObject(FObject& Object)
{
	if (!bInitialized)
	{
		CATTY_CORE_ERROR("FGC::RegisterObject: not initialized");
		return;
	}

	Object.GC = this;
	LiveObjects.push_back(&Object);
}

FObjectRef FGC::FindPackage(const std::string& PackageName) const
{
	if (!bInitialized)
	{
		return {};
	}

	const std::string Key = FPaths::NormalizePackagePath(PackageName);
	if (Key.empty())
	{
		return {};
	}

	for (FObject* Object : LiveObjects)
	{
		if (!Object || Object->IsPendingKill())
		{
			continue;
		}

		auto* Package = dynamic_cast<FPackage*>(Object);
		if (!Package)
		{
			continue;
		}

		if (FPaths::NormalizePackagePath(Package->GetName()) == Key)
		{
			return FObjectRef::Wrap(Package);
		}
	}

	return {};
}

void FGC::UnregisterObject(FObject& Object)
{
	LiveObjects.erase(
		std::remove(LiveObjects.begin(), LiveObjects.end(), &Object),
		LiveObjects.end());
	RemoveAllRootRefs(&Object);
	RemoveFromPendingKill(&Object);
	Object.GC = nullptr;
}

void FGC::RemoveAllRootRefs(FObject* Object)
{
	if (!Object)
	{
		return;
	}

	RootMap.erase(Object);
}

void FGC::RemoveFromPendingKill(FObject* Object)
{
	PendingKill.erase(std::remove(PendingKill.begin(), PendingKill.end(), Object), PendingKill.end());
}

bool FGC::IsKeptAlive(const FObject& Object)
{
	return Object.GetRefCount() > 0;
}

bool FGC::IsInRootSet(const FObject& Object) const
{
	return RootMap.find(const_cast<FObject*>(&Object)) != RootMap.end();
}

void FGC::FinalizeDeadObject(FObject* Object)
{
	if (!Object)
	{
		return;
	}

	if (IsKeptAlive(*Object))
	{
		CATTY_CORE_ERROR(
			"FGC::FinalizeDeadObject: '{}' still has RefCount {} — refuse finalize",
			Object->GetPathName(),
			Object->GetRefCount());
		return;
	}

	if (!TearDownPooledObject(Object))
	{
		CATTY_CORE_ERROR(
			"FGC::FinalizeDeadObject: no pooled type claimed TearDown for '{}'",
			Object->GetPathName());
	}

	UnregisterObject(*Object);

	if (!FreePooledObject(Object))
	{
		CATTY_CORE_ERROR(
			"FGC::FinalizeDeadObject: no pooled type claimed Free for '{}'",
			Object->GetPathName());
	}
}

bool FGC::TearDownPooledObject(FObject* Object)
{
	if (!Object)
	{
		return false;
	}

	for (auto& Pair : PooledTypes)
	{
		if (Pair.second && Pair.second->TryTearDown(Object))
		{
			return true;
		}
	}
	return false;
}

bool FGC::FreePooledObject(FObject* Object)
{
	if (!Object)
	{
		return false;
	}

	for (auto& Pair : PooledTypes)
	{
		if (Pair.second && Pair.second->TryFree(Object))
		{
			return true;
		}
	}
	return false;
}

void FGC::AddToRoot(FObject& Object)
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
}

void FGC::RemoveFromRoot(FObject& Object)
{
	const auto It = RootMap.find(&Object);
	if (It == RootMap.end())
	{
		CATTY_CORE_WARN("FGC::RemoveFromRoot: '{}' is not in the root map", Object.GetPathName());
		return;
	}

	if (It->second.NestCount > 1)
	{
		--It->second.NestCount;
		return;
	}

	RootMap.erase(It);
}

void FGC::EnqueuePendingKill(FObject& Object)
{
	Object.AddFlags(EObjectFlags::PendingKill);

	if (std::find(PendingKill.begin(), PendingKill.end(), &Object) == PendingKill.end())
	{
		PendingKill.push_back(&Object);
	}
}

void FGC::QueueUnreferenced()
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

void FGC::CollectGarbage()
{
	if (!bInitialized)
	{
		return;
	}

	QueueUnreferenced();
}

void FGC::PurgePendingKill()
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
		FinalizeDeadObject(Object);
	}
}

void FGC::Tick(float DeltaSeconds)
{
	if (!bInitialized)
	{
		return;
	}

	// Allow runtime ini/console changes to take effect.
	CollectIntervalSeconds = GCVarCollectInterval.GetValue();
	PurgeIntervalSeconds = GCVarPurgeInterval.GetValue();

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

namespace Detail
{

FGC* GetGC()
{
	if (!GApp)
	{
		return nullptr;
	}
	FGCModule* Module = GApp->GetModule<FGCModule>();
	return Module ? &Module->GetGC() : nullptr;
}

} // namespace Detail

} // namespace Catty
