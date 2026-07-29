#include <Core/Modules/GC.h>

#include <Core/ConsoleManager.h>
#include <Core/App.h>
#include <Core/Log.h>
#include <Core/Paths.h>
#include <Core/Package.h>
#include <Core/SoftObjectPath.h>
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

	CollectGarbage();
	PurgePendingKill();

	if (!LiveObjects.empty() || !PendingKill.empty())
	{
		CATTY_CORE_ERROR(
			"FGC::Shutdown: refuse — {} live / {} pending-kill object(s); "
			"FApp must WaitForExit until GC IsIdle before unloading GC module",
			LiveObjects.size(),
			PendingKill.size());
		return;
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

FObjectRef FGC::FindObject(const std::string& PackageName, const std::string& ObjectName) const
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
		if (FPackage* Package = PackageRef.Cast<FPackage>())
		{
			if (FObjectRef Found = Package->FindObject(ObjectName))
			{
				return Found;
			}
		}
	}

	// Fallback: LiveObjects is authoritative even if package name table missed.
	for (FObject* Object : LiveObjects)
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

FObjectRef FGC::FindObject(const std::string& PathName) const
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
			CATTY_CORE_WARN(
				"FGC::FindObject: subobject path not implemented yet ('{}') — resolving asset only",
				SoftPath.ToStringWithoutClass());
		}
		return FindObject(SoftPath.GetPackageName(), SoftPath.GetAssetName());
	}

	return FindPackage(PathName);
}

void FGC::UnregisterObject(FObject& Object)
{
	LiveObjects.erase(
		std::remove(LiveObjects.begin(), LiveObjects.end(), &Object),
		LiveObjects.end());
	RemoveFromPendingKill(&Object);
	Object.GC = nullptr;
}

void FGC::RemoveFromPendingKill(FObject* Object)
{
	PendingKill.erase(std::remove(PendingKill.begin(), PendingKill.end(), Object), PendingKill.end());
}

bool FGC::IsKeptAlive(const FObject& Object)
{
	return Object.GetRefCount() > 0;
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

bool FGC::IsIdle() const
{
	return !bInitialized || (LiveObjects.empty() && PendingKill.empty());
}

bool FGC::ExecuteStage(EModuleStage Stage, FApp& App, FStageContext& Ctx)
{
	(void)App;
	switch (Stage)
	{
	case EModuleStage::Init:
		if (!Initialize())
		{
			CATTY_CORE_ERROR("FGC: Initialize failed");
			return false;
		}
		if (!IsInitialized())
		{
			CATTY_CORE_ERROR("FGC: must be initialized after Init");
			return false;
		}
		return true;
	case EModuleStage::Update:
		Tick(Ctx.DeltaSeconds);
		return true;
	case EModuleStage::PrepareExit:
		CollectGarbage();
		PurgePendingKill();
		return true;
	case EModuleStage::Shutdown:
		if (IsInitialized())
		{
			Shutdown();
		}
		return true;
	default:
		return true;
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
	return GApp->GetModule<FGC>();
}

} // namespace Detail

} // namespace Catty
