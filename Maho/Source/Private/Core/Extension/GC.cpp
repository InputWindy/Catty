#include <Core/Extension/GC.h>

#include <Core/System/Console.h>
#include <Core/Application/App.h>
#include <Core/System/Log.h>
#include <Core/System/Paths.h>
#include <Core/Object/Package.h>
#include <Core/Object/SoftObjectPath.h>
#include <ObjectReflectTypes.gen.h>

#include <algorithm>

namespace Maho
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

FGCSystem::~FGCSystem()
{
	Shutdown();
}

bool FGCSystem::Initialize()
{
	if (bInitialized)
	{
		return true;
	}

	CollectIntervalSeconds = GCVarCollectInterval.GetValue();
	PurgeIntervalSeconds = GCVarPurgeInterval.GetValue();
	bInitialized = true;
	RegisterGeneratedGCPooledTypes(*this);
	MAHO_CORE_INFO("GC initialized");
	return true;
}

void FGCSystem::Shutdown()
{
	if (!bInitialized)
	{
		return;
	}

	CollectGarbage();
	PurgePendingKill();

	if (!LiveObjects.empty() || !PendingKill.empty())
	{
		MAHO_CORE_ERROR(
			"FGCSystem::Shutdown: refuse — {} live / {} pending-kill object(s); "
			"FApp must WaitForExit until GC IsIdle before unloading GC module",
			LiveObjects.size(),
			PendingKill.size());
		return;
	}

	LiveObjects.clear();
	PendingKill.clear();
	PooledTypes.clear();
	bInitialized = false;
	MAHO_CORE_INFO("GC shut down");
}

void FGCSystem::RegisterObject(UObject& Object)
{
	if (!bInitialized)
	{
		MAHO_CORE_ERROR("FGCSystem::RegisterObject: not initialized");
		return;
	}

	Object.GC = this;
	LiveObjects.push_back(&Object);
}

FObjectRef FGCSystem::FindPackage(const std::string& PackageName) const
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

	for (UObject* Object : LiveObjects)
	{
		if (!Object || Object->IsPendingKill())
		{
			continue;
		}

		auto* Package = dynamic_cast<UPackage*>(Object);
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

FObjectRef FGCSystem::FindObject(const std::string& PackageName, const std::string& ObjectName) const
{
	if (!bInitialized || ObjectName.empty())
	{
		return {};
	}

	const std::string PkgKey = FPaths::NormalizePackagePath(PackageName);
	if (PkgKey.empty())
	{
		return {};
	}

	if (FObjectRef PackageRef = FindPackage(PkgKey))
	{
		if (UPackage* Package = PackageRef.Cast<UPackage>())
		{
			if (FObjectRef Found = Package->FindObject(ObjectName))
			{
				return Found;
			}
		}
	}

	// Fallback: LiveObjects is authoritative even if package name table missed.
	for (UObject* Object : LiveObjects)
	{
		if (!Object || Object->IsPendingKill())
		{
			continue;
		}

		FSoftObjectPath SoftPath;
		if (!SoftPath.TrySetPath(Object->GetPathName()) || !SoftPath.IsValid())
		{
			continue;
		}
		if (FPaths::NormalizePackagePath(SoftPath.GetPackageName()) == PkgKey
			&& SoftPath.GetAssetName() == ObjectName)
		{
			return FObjectRef::Wrap(Object);
		}
	}

	return {};
}

FObjectRef FGCSystem::FindObject(const std::string& PathName) const
{
	if (!bInitialized || PathName.empty())
	{
		return {};
	}

	FSoftObjectPath SoftPath;
	if (SoftPath.TrySetPath(PathName) && SoftPath.IsValid())
	{
		if (SoftPath.HasSubPath())
		{
			MAHO_CORE_WARN(
				"FGCSystem::FindObject: subobject path not implemented yet ('{}') — resolving asset only",
				SoftPath.ToStringWithoutClass());
		}
		return FindObject(SoftPath.GetPackageName(), SoftPath.GetAssetName());
	}

	return FindPackage(PathName);
}

void FGCSystem::UnregisterObject(UObject& Object)
{
	LiveObjects.erase(
		std::remove(LiveObjects.begin(), LiveObjects.end(), &Object),
		LiveObjects.end());
	RemoveFromPendingKill(&Object);
	Object.GC = nullptr;
}

void FGCSystem::RemoveFromPendingKill(UObject* Object)
{
	PendingKill.erase(std::remove(PendingKill.begin(), PendingKill.end(), Object), PendingKill.end());
}

bool FGCSystem::IsKeptAlive(const UObject& Object)
{
	return Object.GetRefCount() > 0;
}

void FGCSystem::FinalizeDeadObject(UObject* Object)
{
	if (!Object)
	{
		return;
	}

	if (IsKeptAlive(*Object))
	{
		MAHO_CORE_ERROR(
			"FGCSystem::FinalizeDeadObject: '{}' still has RefCount {} — refuse finalize",
			Object->GetPathName(),
			Object->GetRefCount());
		return;
	}

	if (!TearDownPooledObject(Object))
	{
		MAHO_CORE_ERROR(
			"FGCSystem::FinalizeDeadObject: no pooled type claimed TearDown for '{}'",
			Object->GetPathName());
	}

	UnregisterObject(*Object);

	if (!FreePooledObject(Object))
	{
		MAHO_CORE_ERROR(
			"FGCSystem::FinalizeDeadObject: no pooled type claimed Free for '{}'",
			Object->GetPathName());
	}
}

bool FGCSystem::IsIdle() const
{
	return !bInitialized || (LiveObjects.empty() && PendingKill.empty());
}

bool FGCSystem::ExecuteStage(EEngineStage Stage)
{
	switch (Stage)
	{
	case EEngineStage::Init:
		if (!Initialize())
		{
			MAHO_CORE_ERROR("FGCSystem: Initialize failed");
			return false;
		}
		if (!IsInitialized())
		{
			MAHO_CORE_ERROR("FGCSystem: must be initialized after Init");
			return false;
		}
		return true;
	case EEngineStage::Update:
		if (GApp)
		{
			Tick(GApp->GetDeltaSeconds());
		}
		return true;
	case EEngineStage::PrepareExit:
		CollectGarbage();
		PurgePendingKill();
		return true;
	case EEngineStage::Shutdown:
		if (IsInitialized())
		{
			Shutdown();
		}
		return true;
	default:
		return true;
	}
}

bool FGCSystem::TearDownPooledObject(UObject* Object)
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

bool FGCSystem::FreePooledObject(UObject* Object)
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

void FGCSystem::QueueUnreferenced()
{
	for (UObject* Object : LiveObjects)
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

void FGCSystem::CollectGarbage()
{
	if (!bInitialized)
	{
		return;
	}

	QueueUnreferenced();
}

void FGCSystem::PurgePendingKill()
{
	if (!bInitialized || PendingKill.empty())
	{
		return;
	}

	std::vector<UObject*> ToFree;
	ToFree.reserve(PendingKill.size());

	for (UObject* Object : PendingKill)
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

	for (UObject* Object : ToFree)
	{
		MAHO_CORE_INFO("GC purge pending kill: {}", Object->GetPathName());
		FinalizeDeadObject(Object);
	}
}

void FGCSystem::Tick(float DeltaSeconds)
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

FGCSystem* GetGCSystem()
{
	if (!GApp)
	{
		return nullptr;
	}
	return GApp->GetExtension<FGCSystem>();
}

} // namespace Detail

} // namespace Maho
