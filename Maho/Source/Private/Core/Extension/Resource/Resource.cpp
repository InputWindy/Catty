#include "ResourceIO.h"
#include "ResourceCasset.h"

#include <Core/Application/App.h>
#include <Core/Extension/GC/GC.h>
#include <Core/Json.h>
#include <Core/System/Log.h>
#include <Core/System/Paths.h>
#include <Core/System/Utf8Path.h>
#include <Core/Object/Package.h>
#include <Core/Object/SoftObjectPath.h>
#include "ResourceServer.h"

#include <ResourceTypes.gen.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Maho
{

FResourceSystem::FResourceSystem()
	: Server(std::make_unique<FResourceServer>())
{
}

FResourceSystem::~FResourceSystem()
{
	Shutdown();
}

bool FResourceSystem::IsInitialized() const
{
	return Server && Server->IsInitialized();
}

std::string FResourceSystem::NormalizePackageName(std::string Name)
{
	for (char& Ch : Name)
	{
		if (Ch == '\\')
		{
			Ch = '/';
		}
	}
	while (Name.size() >= 2 && Name[0] == '.' && Name[1] == '/')
	{
		Name.erase(0, 2);
	}
	return Name;
}

std::string FResourceSystem::NormalizeSourcePath(std::string Path)
{
	for (char& Ch : Path)
	{
		if (Ch == '\\')
		{
			Ch = '/';
		}
	}
#if defined(_WIN32)
	// ASCII-only — byte-wise tolower corrupts UTF-8 CJK paths.
	AsciiToLowerInPlace(Path);
#endif

	while (Path.size() >= 2 && Path[0] == '.' && Path[1] == '/')
	{
		Path.erase(0, 2);
	}

	return Path;
}

std::string FResourceSystem::MakeObjectNameFromSource(const std::string& SourcePath)
{
	const std::size_t Slash = SourcePath.find_last_of("/\\");
	const std::size_t Start = (Slash == std::string::npos) ? 0 : Slash + 1;
	std::string Stem = SourcePath.substr(Start);
	const std::size_t Dot = Stem.find_last_of('.');
	if (Dot != std::string::npos)
	{
		Stem.resize(Dot);
	}
	return Stem.empty() ? std::string("Resource") : Stem;
}

bool FResourceSystem::Initialize()
{
	if (IsInitialized())
	{
		return true;
	}

	FGCSystem* GC = Detail::GetGCSystem();
	if (!GC || !GC->IsInitialized())
	{
		MAHO_CORE_ERROR("FResourceSystem::Initialize: FGCSystem must be initialized first");
		return false;
	}

	if (!Server->Initialize())
	{
		MAHO_CORE_ERROR("FResourceSystem::Initialize: FResourceServer failed");
		return false;
	}

	RegisterGeneratedResourceTypes(*this, *GC);

	bAcceptingNewWork = true;
	MAHO_CORE_INFO("FResourceSystem initialized");
	return true;
}

bool FResourceSystem::RegisterResource(const FObjectRef& Resource)
{
	if (!bAcceptingNewWork)
	{
		MAHO_CORE_ERROR("FResourceSystem::RegisterResource: refused during exit");
		return false;
	}

	UResource* ResourcePtr = Resource.Cast<UResource>();
	if (!ResourcePtr)
	{
		MAHO_CORE_ERROR("FResourceSystem::RegisterResource: Ref is not an UResource");
		return false;
	}

	const std::string CatalogKey = MakeResourceCatalogKey(*ResourcePtr);
	if (CatalogKey.empty())
	{
		MAHO_CORE_ERROR(
			"FResourceSystem::RegisterResource: empty catalog key for '{}'",
			ResourcePtr->GetName());
		return false;
	}

	const auto Existing = Resources.find(CatalogKey);
	if (Existing != Resources.end() && Existing->second && Existing->second.Get() != ResourcePtr)
	{
		MAHO_CORE_ERROR(
			"FResourceSystem::RegisterResource: '{}' already registered to another object",
			CatalogKey);
		return false;
	}

	Resources[CatalogKey] = Resource;
	if (!Resources[CatalogKey].IsValid())
	{
		// Copy may have skipped AddRef if liveness check raced; force a hold on the known pointer.
		Resources[CatalogKey] = FObjectRef::Wrap(ResourcePtr);
	}
	if (!Resources[CatalogKey].IsValid())
	{
		MAHO_CORE_ERROR(
			"FResourceSystem::RegisterResource: failed to pin '{}' in catalog",
			CatalogKey);
		Resources.erase(CatalogKey);
		return false;
	}
	return true;
}

bool FResourceSystem::RegisterOwnedResource(UPackage& Package, const FObjectRef& Resource)
{
	UObject* Object = Resource.Get();
	if (!Object)
	{
		MAHO_CORE_ERROR("FResourceSystem::RegisterOwnedResource: null Resource");
		return false;
	}

	if (!Package.RegisterObject(Object))
	{
		MAHO_CORE_ERROR(
			"FResourceSystem::RegisterOwnedResource: Package.RegisterObject failed for '{}'",
			Object->GetName());
		Object->ClearOuter();
		return false;
	}

	if (!RegisterResource(Resource))
	{
		Package.DetachObject(Object);
		Object->ClearOuter();
		return false;
	}
	return true;
}

bool FResourceSystem::UnregisterResource(UObject* Resource)
{
	UResource* ResourcePtr = dynamic_cast<UResource*>(Resource);
	if (!ResourcePtr)
	{
		return false;
	}

	CancelPendingImport(ResourcePtr);

	const std::string Key = MakeResourceCatalogKey(*ResourcePtr);
	if (Key.empty())
	{
		return false;
	}

	const auto It = Resources.find(Key);
	if (It == Resources.end() || It->second.Get() != static_cast<UObject*>(ResourcePtr))
	{
		return false;
	}

	Resources.erase(It);
	ResourcePtr->ClearOuter();
	return true;
}

bool FResourceSystem::UnregisterResource(const FObjectRef& Resource)
{
	return UnregisterResource(Resource.Get());
}

void FResourceSystem::AbortFailedImport(UResource& Resource)
{
	FObjectRef PackageRef = Resource.GetPackage();
	UPackage* Package = PackageRef.Cast<UPackage>();
	if (Package)
	{
		UnregisterResourcesInPackage(Package->GetName());
	}
	else
	{
		UnregisterResource(&Resource);
	}
}

std::string FResourceSystem::MakeResourceCatalogKey(const UResource& Resource)
{
	return NormalizeResourceVirtualPath(Resource.GetPathName());
}

std::string FResourceSystem::NormalizeResourceVirtualPath(const std::string& VirtualPath)
{
	FSoftObjectPath SoftPath;
	if (SoftPath.TrySetPath(VirtualPath) && SoftPath.IsValid())
	{
		return SoftPath.GetAssetPathString();
	}

	std::string Path = VirtualPath;
	for (char& Ch : Path)
	{
		if (Ch == '\\')
		{
			Ch = '/';
		}
	}
	return Path;
}

void FResourceSystem::ForEachRegisteredResource(
	const std::function<void(const std::string& CatalogKey, const FObjectRef& Resource)>& Fn) const
{
	if (!Fn)
	{
		return;
	}
	for (const auto& Pair : Resources)
	{
		if (Pair.second)
		{
			Fn(Pair.first, Pair.second);
		}
	}
}

FObjectRef FResourceSystem::FindRegisteredResource(const std::string& CatalogKey) const
{
	const std::string Key = NormalizeResourceVirtualPath(CatalogKey);
	if (Key.empty())
	{
		return {};
	}
	const auto It = Resources.find(Key);
	if (It == Resources.end())
	{
		return {};
	}
	return It->second;
}

std::string FResourceSystem::SoftPathKey(const FSoftObjectPath& SoftPath)
{
	return SoftPath.GetAssetPathString();
}

void FResourceSystem::UnregisterResourcesInPackage(const std::string& PackageName)
{
	const std::string Key = NormalizePackageName(PackageName);
	std::vector<UObject*> Snapshot;
	for (const auto& Pair : Resources)
	{
		UResource* Resource = Pair.second.Cast<UResource>();
		if (!Resource)
		{
			continue;
		}

		FObjectRef PackageRef = Resource->GetPackage();
		UPackage* Package = PackageRef.Cast<UPackage>();
		if (Package && NormalizePackageName(Package->GetName()) == Key)
		{
			Snapshot.push_back(Resource);
		}
	}

	for (UObject* Resource : Snapshot)
	{
		UnregisterResource(Resource);
	}
}

void FResourceSystem::PrepareForExit()
{
	if (!IsInitialized())
	{
		return;
	}

	bAcceptingNewWork = false;
	if (AsyncSave.Task)
	{
		AsyncSave.Task->Wait();
		AsyncSave = {};
	}
	FlushAll();

	for (auto& Pair : PendingIO)
	{
		ReleaseBulkLoad(Pair.second.Handle);
	}
	PendingIO.clear();

	std::vector<FObjectRef> Snapshot;
	Snapshot.reserve(Resources.size());
	for (auto& Pair : Resources)
	{
		Snapshot.push_back(Pair.second);
	}

	for (FObjectRef& Ref : Snapshot)
	{
		if (UResource* Resource = Ref.Cast<UResource>())
		{
			UnregisterResource(Resource);
			Resource->ClearOuter();
		}
	}

	Resources.clear();
	Snapshot.clear();
}

bool FResourceSystem::IsIdle() const
{
	if (!IsInitialized())
	{
		return true;
	}
	return !bAcceptingNewWork
		&& Resources.empty()
		&& PendingIO.empty()
		&& !AsyncSave.bActive
		&& !Server->HasPendingLoads();
}

bool FResourceSystem::ExecuteStage(EEngineStage Stage)
{
	switch (Stage)
	{
	case EEngineStage::Init:
		if (!Initialize())
		{
			MAHO_CORE_ERROR("FResourceSystem: Initialize failed");
			return false;
		}
		return true;
	case EEngineStage::BeginFrame:
	case EEngineStage::Update:
		ProcessReadyIO();
		TickSavePackage();
		return true;
	case EEngineStage::PrepareExit:
		PrepareForExit();
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

void FResourceSystem::Shutdown()
{
	if (!IsInitialized())
	{
		return;
	}

	PrepareForExit();

	if (Server->IsInitialized())
	{
		Server->Shutdown();
	}

	MAHO_CORE_INFO("FResourceSystem shut down");
}

void FResourceSystem::CancelPendingImport(UObject* Resource)
{
	if (!Resource)
	{
		return;
	}

	const FSoftObjectPath Soft = FSoftObjectPath::FromObject(*Resource);
	const std::string Key = SoftPathKey(Soft);
	if (Key.empty())
	{
		return;
	}

	const auto It = PendingIO.find(Key);
	if (It == PendingIO.end())
	{
		return;
	}

	ReleaseBulkLoad(It->second.Handle);
	PendingIO.erase(It);
}

void FResourceSystem::ProcessReadyIO()
{
	if (!HasActiveServer() || PendingIO.empty())
	{
		return;
	}

	std::vector<std::string> ReadyKeys;
	ReadyKeys.reserve(PendingIO.size());
	for (const auto& Pair : PendingIO)
	{
		const FTransferHandle Handle = Pair.second.Handle;
		if (!Handle.IsValid() || Handle.HasFailed() || Handle.HasSucceeded())
		{
			ReadyKeys.push_back(Pair.first);
		}
	}

	// At most one Apply per tick so large .casset hydrates do not stall the frame.
	std::size_t Applied = 0;
	constexpr std::size_t kMaxAppliesPerTick = 1;
	for (const std::string& Key : ReadyKeys)
	{
		if (Applied >= kMaxAppliesPerTick)
		{
			break;
		}

		const auto It = PendingIO.find(Key);
		if (It == PendingIO.end())
		{
			continue;
		}

		FPendingIO Pending = std::move(It->second);
		PendingIO.erase(It);

		if (!Pending.Handle.IsValid() || Pending.Handle.HasFailed())
		{
			MAHO_CORE_ERROR(
				"FResourceSystem::ProcessReadyIO: BulkData failed for '{}'",
				Pending.Config.SourcePath);
			ReleaseBulkLoad(Pending.Handle);
			continue;
		}

		FResourceBulkData Bulk;
		if (!TakeBulkData(Pending.Handle, Bulk))
		{
			MAHO_CORE_ERROR(
				"FResourceSystem::ProcessReadyIO: TakeBulkData failed for '{}'",
				Pending.Config.SourcePath);
			ReleaseBulkLoad(Pending.Handle);
			continue;
		}

		const bool bOk = Pending.Importer
			&& Pending.Importer->ApplyBulkData(*this, Pending.Config, Bulk);
		ReleaseBulkLoad(Pending.Handle);
		++Applied;

		if (!bOk)
		{
			MAHO_CORE_ERROR(
				"FResourceSystem::ProcessReadyIO: Apply failed for '{}'",
				Pending.Config.SourcePath);
		}
	}
}

bool FResourceSystem::HasActiveServer() const
{
	return Server && Server->IsInitialized();
}

FTransferHandle FResourceSystem::RequestBulkLoad(const std::string& SourcePath)
{
	if (!HasActiveServer())
	{
		return {};
	}
	return Server->RequestLoad(SourcePath);
}

void FResourceSystem::ReleaseBulkLoad(FTransferHandle Handle)
{
	if (HasActiveServer() && Handle.IsValid())
	{
		Server->Release(Handle);
	}
}

bool FResourceSystem::TakeBulkData(FTransferHandle Handle, FResourceBulkData& OutBulk)
{
	if (!HasActiveServer() || !Handle.IsValid())
	{
		return false;
	}
	return Server->TryTakeBulkData(Handle, OutBulk);
}

FSoftObjectPath FResourceSystem::EnqueueImport(
	std::unique_ptr<IResourceImporter> Importer,
	FResourceImportConfig Config)
{
	if (!Importer)
	{
		MAHO_CORE_ERROR("FResourceSystem::EnqueueImport: null Importer");
		return {};
	}

	if (!IsInitialized() || !bAcceptingNewWork)
	{
		MAHO_CORE_ERROR("FResourceSystem::EnqueueImport: not accepting work");
		return {};
	}

	Config.SourcePath = NormalizeSourcePath(std::move(Config.SourcePath));
	Config.PackagePath = NormalizePackageName(std::move(Config.PackagePath));
	if (Config.ObjectName.empty() && !Config.SourcePath.empty())
	{
		Config.ObjectName = MakeObjectNameFromSource(Config.SourcePath);
	}

	if (Config.PackagePath.empty() || Config.ObjectName.empty() || Config.SourcePath.empty())
	{
		MAHO_CORE_ERROR(
			"FResourceSystem::EnqueueImport: PackagePath/ObjectName/SourcePath required");
		return {};
	}

	FSoftObjectPath SoftPath(Config.PackagePath, Config.ObjectName);
	const std::string Key = SoftPathKey(SoftPath);
	if (Key.empty())
	{
		return {};
	}

	if (PendingIO.find(Key) != PendingIO.end())
	{
		return SoftPath;
	}

	if (!HasActiveServer())
	{
		MAHO_CORE_ERROR("FResourceSystem::EnqueueImport: ResourceServer unavailable");
		return {};
	}

	FTransferHandle Handle = RequestBulkLoad(Config.SourcePath);
	if (!Handle.IsValid())
	{
		return {};
	}

	FPendingIO Pending;
	Pending.Handle = Handle;
	Pending.SoftPath = SoftPath;
	Pending.Config = Config;
	Pending.Importer = std::move(Importer);
	PendingIO.emplace(Key, std::move(Pending));

	if (Config.Mode == EResourceIOMode::Sync)
	{
		Flush(SoftPath);
	}

	return SoftPath;
}

bool FResourceSystem::UnloadResource(const std::string& VirtualPath)
{
	FGCSystem* GC = Detail::GetGCSystem();
	FObjectRef Found = GC ? GC->FindObject(VirtualPath) : FObjectRef{};
	UResource* ResourcePtr = Found.Cast<UResource>();
	if (!ResourcePtr)
	{
		return false;
	}

	return UnregisterResource(ResourcePtr);
}

bool FResourceSystem::UnloadResource(const FObjectRef& Resource)
{
	UResource* ResourcePtr = Resource.Cast<UResource>();
	if (!ResourcePtr)
	{
		return false;
	}

	return UnregisterResource(ResourcePtr);
}

FObjectRef FResourceSystem::TryLoad(const FSoftObjectPath& SoftPath)
{
	if (!SoftPath.IsValid())
	{
		return {};
	}

	if (FObjectRef Existing = SoftPath.Resolve())
	{
		return Existing;
	}

	FGCSystem* GC = Detail::GetGCSystem();
	if (GC && GC->FindPackage(SoftPath.GetPackageName()))
	{
		return SoftPath.Resolve();
	}

	const std::string Filename = FPaths::ConvertPackageNameToFilename(SoftPath.GetPackageName());
	if (Filename.empty())
	{
		MAHO_CORE_ERROR(
			"FResourceSystem::TryLoad: no mount mapping for '{}'",
			SoftPath.GetPackageName());
		return {};
	}

	FResourceImportConfig Config;
	Config.PackagePath = SoftPath.GetPackageName();
	Config.ObjectName = SoftPath.GetAssetName();
	Config.SourcePath = Filename;
	Config.Mode = EResourceIOMode::Sync;
	(void)Import<FCassetPackageImporter>(std::move(Config));

	return SoftPath.Resolve();
}

FObjectRef FResourceSystem::TryLoad(const std::string& SoftPathString)
{
	FSoftObjectPath SoftPath;
	if (!SoftPath.TrySetPath(SoftPathString) || !SoftPath.IsValid())
	{
		return {};
	}
	return TryLoad(SoftPath);
}

bool FResourceSystem::SavePackage(
	const FObjectRef& Package,
	const std::string& FilePath,
	bool bPretty,
	bool bSaveDependencies)
{
	std::unordered_set<std::string> SavingPackageNames;
	return SavePackageInternal(Package, FilePath, bPretty, bSaveDependencies, SavingPackageNames);
}

bool FResourceSystem::EnqueueSavePackage(
	const FObjectRef& Package,
	const std::string& FilePath,
	bool bSaveDependencies)
{
	if (AsyncSave.bActive)
	{
		MAHO_CORE_WARN("FResourceSystem::EnqueueSavePackage: save already in progress");
		return false;
	}
	if (!IsInitialized() || !bAcceptingNewWork)
	{
		MAHO_CORE_ERROR("FResourceSystem::EnqueueSavePackage: not accepting work");
		return false;
	}
	if (!Package)
	{
		return false;
	}

	UPackage* PackagePtr = Package.Cast<UPackage>();
	if (!PackagePtr || !PackagePtr->IsPersistent())
	{
		MAHO_CORE_ERROR("FResourceSystem::EnqueueSavePackage: invalid / non-persistent package");
		return false;
	}

	std::string OutPath = FilePath.empty() ? PackagePtr->GetFilePath() : FilePath;
	if (OutPath.empty())
	{
		MAHO_CORE_ERROR("FResourceSystem::EnqueueSavePackage: empty file path");
		return false;
	}

	if (bSaveDependencies)
	{
		std::unordered_set<std::string> SavingPackageNames;
		std::unordered_map<std::string, std::string> DependencyNameToFile;
		for (const auto& Pair : PackagePtr->Objects)
		{
			UObject* Object = Pair.second;
			if (!Object)
			{
				continue;
			}
			std::vector<UObject*> Referenced;
			Object->GetReferencedObjects(Referenced);
			for (UObject* RefObj : Referenced)
			{
				if (!RefObj)
				{
					continue;
				}
				FObjectRef OtherOuter = RefObj->GetOuter();
				UPackage* OtherPackage = OtherOuter.Cast<UPackage>();
				if (!OtherPackage || OtherPackage == PackagePtr || !OtherPackage->IsPersistent())
				{
					continue;
				}
				DependencyNameToFile[OtherPackage->GetName()] = OtherPackage->GetFilePath();
			}
		}

		for (const auto& Dep : DependencyNameToFile)
		{
			FGCSystem* GC = Detail::GetGCSystem();
			FObjectRef DepPackage = GC ? GC->FindPackage(NormalizePackageName(Dep.first)) : FObjectRef{};
			UPackage* DepPtr = DepPackage.Cast<UPackage>();
			if (!DepPtr)
			{
				MAHO_CORE_ERROR("FResourceSystem::EnqueueSavePackage: dependency '{}' not loaded", Dep.first);
				return false;
			}
			std::string DepFile = Dep.second.empty() ? DepPtr->GetFilePath() : Dep.second;
			if (DepFile.empty()
				|| !SavePackageInternal(DepPackage, DepFile, true, true, SavingPackageNames))
			{
				MAHO_CORE_ERROR(
					"FResourceSystem::EnqueueSavePackage: failed saving dependency '{}'",
					Dep.first);
				return false;
			}
		}
	}

	{
		std::error_code ErrorCode;
		const std::filesystem::path Parent = PathFromUtf8(OutPath).parent_path();
		if (!Parent.empty())
		{
			std::filesystem::create_directories(Parent, ErrorCode);
			if (ErrorCode)
			{
				MAHO_CORE_ERROR(
					"FResourceSystem::EnqueueSavePackage: create_directories failed: {}",
					ErrorCode.message());
				return false;
			}
		}
	}

	AsyncSave = {};
	AsyncSave.bActive = true;
	AsyncSave.PackageRef = Package;
	AsyncSave.OutPath = OutPath;
	AsyncSave.PackageName = PackagePtr->GetName();
	AsyncSave.Progress = 0.15f;
	AsyncSave.StatusText = "Building package…";
	AsyncSave.FileBytes = std::make_shared<std::vector<std::uint8_t>>();
	AsyncSave.bOk = std::make_shared<std::atomic<bool>>(false);

	if (!ResourceCasset::BuildPackageDocument(*PackagePtr, AsyncSave.Document))
	{
		MAHO_CORE_ERROR("FResourceSystem::EnqueueSavePackage: BuildPackageDocument failed");
		AsyncSave = {};
		return false;
	}

	AsyncSave.Progress = 0.45f;
	AsyncSave.StatusText = "Compressing…";
	PackagePtr->SetFilePath(OutPath);

	auto Document = std::make_shared<std::vector<std::uint8_t>>(std::move(AsyncSave.Document));
	AsyncSave.Document.clear();
	auto FileBytes = AsyncSave.FileBytes;
	auto bOk = AsyncSave.bOk;
	const std::string WritePath = OutPath;

	AsyncSave.Task = FAsyncTask::LaunchNew([Document, FileBytes, bOk, WritePath]()
	{
		std::vector<std::uint8_t> LocalFile;
		if (!ResourceCasset::WrapDocumentToMcasFile(*Document, LocalFile))
		{
			bOk->store(false, std::memory_order_release);
			return;
		}

		std::ofstream Out(PathFromUtf8(WritePath), std::ios::binary | std::ios::trunc);
		if (!Out
			|| !Out.write(
				reinterpret_cast<const char*>(LocalFile.data()),
				static_cast<std::streamsize>(LocalFile.size())))
		{
			bOk->store(false, std::memory_order_release);
			return;
		}

		*FileBytes = std::move(LocalFile);
		bOk->store(true, std::memory_order_release);
	});
	AsyncSave.bCompressStarted = true;
	return true;
}

bool FResourceSystem::IsSavePackageBusy() const
{
	return AsyncSave.bActive;
}

float FResourceSystem::GetSavePackageProgress() const
{
	if (!AsyncSave.bActive)
	{
		return 1.f;
	}
	if (!AsyncSave.bCompressStarted)
	{
		return AsyncSave.Progress;
	}
	if (AsyncSave.Task && !AsyncSave.Task->IsDone())
	{
		return 0.7f;
	}
	return 0.95f;
}

const std::string& FResourceSystem::GetSavePackageStatusText() const
{
	static const std::string IdleText;
	return AsyncSave.bActive ? AsyncSave.StatusText : IdleText;
}

void FResourceSystem::TickSavePackage()
{
	if (!AsyncSave.bActive || !AsyncSave.bCompressStarted || !AsyncSave.Task)
	{
		return;
	}
	if (!AsyncSave.Task->IsDone())
	{
		AsyncSave.StatusText = "Compressing / writing…";
		AsyncSave.Progress = 0.7f;
		return;
	}

	AsyncSave.Task->Wait();
	const bool bOk = AsyncSave.bOk && AsyncSave.bOk->load(std::memory_order_acquire);
	if (bOk)
	{
		FinalizeSavePackageSuccess();
	}
	else
	{
		FinalizeSavePackageFailure();
	}
}

void FResourceSystem::FinalizeSavePackageSuccess()
{
	UPackage* PackagePtr = AsyncSave.PackageRef.Cast<UPackage>();
	const std::size_t ByteCount = AsyncSave.FileBytes ? AsyncSave.FileBytes->size() : 0;
	if (PackagePtr)
	{
		for (const auto& Pair : PackagePtr->Objects)
		{
			if (UResource* Resource = dynamic_cast<UResource*>(Pair.second))
			{
				Resource->ClearDirty();
			}
		}
		MAHO_CORE_INFO(
			"Saved package '{}' ({} objects) -> '{}' ({} bytes)",
			PackagePtr->GetName(),
			PackagePtr->GetObjectCount(),
			AsyncSave.OutPath,
			ByteCount);
	}
	AsyncSave = {};
}

void FResourceSystem::FinalizeSavePackageFailure()
{
	MAHO_CORE_ERROR(
		"FResourceSystem: async save failed for '{}' -> '{}'",
		AsyncSave.PackageName,
		AsyncSave.OutPath);
	AsyncSave = {};
}

bool FResourceSystem::SavePackageInternal(
	const FObjectRef& Package,
	const std::string& FilePath,
	bool bPretty,
	bool bSaveDependencies,
	std::unordered_set<std::string>& SavingPackageNames)
{
	if (!IsInitialized())
	{
		MAHO_CORE_ERROR("FResourceSystem::SavePackage: not initialized");
		return false;
	}

	if (!Package)
	{
		MAHO_CORE_ERROR("FResourceSystem::SavePackage: invalid Package");
		return false;
	}

	UPackage* PackagePtr = Package.Cast<UPackage>();
	if (!PackagePtr)
	{
		MAHO_CORE_ERROR("FResourceSystem::SavePackage: Ref is not an UPackage");
		return false;
	}

	UPackage& PackageObj = *PackagePtr;

	const std::string PackageKey = NormalizePackageName(PackageObj.GetName());
	if (SavingPackageNames.find(PackageKey) != SavingPackageNames.end())
	{
		MAHO_CORE_ERROR(
			"FResourceSystem::SavePackage: cycle detected while saving '{}'",
			PackageKey);
		return false;
	}
	SavingPackageNames.insert(PackageKey);

	const std::string OutPath = FilePath.empty() ? PackageObj.GetFilePath() : FilePath;
	if (OutPath.empty())
	{
		MAHO_CORE_ERROR("FResourceSystem::SavePackage: empty file path for '{}'", PackageKey);
		SavingPackageNames.erase(PackageKey);
		return false;
	}

	if (!PackageObj.IsPersistent())
	{
		MAHO_CORE_ERROR(
			"FResourceSystem::SavePackage: package '{}' is Transient — mark Persistent first",
			PackageObj.GetName());
		SavingPackageNames.erase(PackageKey);
		return false;
	}

	// Ensure FilePath is set before serializing so dependents can record it.
	PackageObj.SetFilePath(OutPath);

	if (bSaveDependencies)
	{
		std::unordered_map<std::string, std::string> DependencyNameToFile;
		for (const auto& Pair : PackageObj.Objects)
		{
			UObject* Object = Pair.second;
			if (!Object)
			{
				continue;
			}
			std::vector<UObject*> Referenced;
			Object->GetReferencedObjects(Referenced);
			for (UObject* RefObj : Referenced)
			{
				if (!RefObj)
				{
					continue;
				}
				FObjectRef OtherOuter = RefObj->GetOuter();
				UPackage* OtherPackage = OtherOuter.Cast<UPackage>();
				if (!OtherPackage || OtherPackage == &PackageObj || !OtherPackage->IsPersistent())
				{
					continue;
				}
				DependencyNameToFile[OtherPackage->GetName()] = OtherPackage->GetFilePath();
			}
		}

		for (const auto& Dep : DependencyNameToFile)
		{
			const std::string DepName = NormalizePackageName(Dep.first);
			std::string DepFile = Dep.second;
			FGCSystem* GC = Detail::GetGCSystem();
			FObjectRef DepPackage = GC ? GC->FindPackage(DepName) : FObjectRef{};
			UPackage* DepPtr = DepPackage.Cast<UPackage>();
			if (!DepPtr)
			{
				MAHO_CORE_ERROR(
					"FResourceSystem::SavePackage: dependency '{}' not loaded",
					DepName);
				SavingPackageNames.erase(PackageKey);
				return false;
			}
			if (DepFile.empty())
			{
				DepFile = DepPtr->GetFilePath();
			}
			if (DepFile.empty())
			{
				MAHO_CORE_ERROR(
					"FResourceSystem::SavePackage: dependency '{}' has no file path",
					DepName);
				SavingPackageNames.erase(PackageKey);
				return false;
			}
			if (!SavePackageInternal(DepPackage, DepFile, bPretty, true, SavingPackageNames))
			{
				MAHO_CORE_ERROR(
					"FResourceSystem::SavePackage: failed saving dependency '{}' for '{}'",
					DepName,
					PackageKey);
				SavingPackageNames.erase(PackageKey);
				return false;
			}
		}
	}

	{
		std::error_code ErrorCode;
		const std::filesystem::path Parent = PathFromUtf8(OutPath).parent_path();
		if (!Parent.empty())
		{
			std::filesystem::create_directories(Parent, ErrorCode);
			if (ErrorCode)
			{
				MAHO_CORE_ERROR(
					"FResourceSystem::SavePackage: create_directories failed for '{}': {}",
					PathToUtf8(Parent),
					ErrorCode.message());
				SavingPackageNames.erase(PackageKey);
				return false;
			}
		}
	}

	std::vector<std::uint8_t> FileBytes;
	if (!ResourceCasset::EncodePackageFile(PackageObj, FileBytes))
	{
		MAHO_CORE_ERROR("FResourceSystem::SavePackage: EncodePackageFile failed for '{}'", PackageObj.GetName());
		SavingPackageNames.erase(PackageKey);
		return false;
	}

	{
		std::ofstream Out(PathFromUtf8(OutPath), std::ios::binary | std::ios::trunc);
		if (!Out || !Out.write(reinterpret_cast<const char*>(FileBytes.data()), static_cast<std::streamsize>(FileBytes.size())))
		{
			MAHO_CORE_ERROR("FResourceSystem::SavePackage: write failed '{}'", OutPath);
			SavingPackageNames.erase(PackageKey);
			return false;
		}
	}

	for (const auto& Pair : PackageObj.Objects)
	{
		if (UResource* Resource = dynamic_cast<UResource*>(Pair.second))
		{
			Resource->ClearDirty();
		}
	}

	MAHO_CORE_INFO(
		"Saved package '{}' ({} objects) -> '{}' ({} bytes)",
		PackageObj.GetName(),
		PackageObj.GetObjectCount(),
		OutPath,
		FileBytes.size());
	SavingPackageNames.erase(PackageKey);
	return true;
}

FObjectRef FResourceSystem::ResolveObjectPath(const std::string& PathName) const
{
	FGCSystem* GC = Detail::GetGCSystem();
	return GC ? GC->FindObject(PathName) : FObjectRef{};
}

FObjectRef FResourceSystem::LoadPackage(const std::string& FilePath)
{
	std::unordered_set<std::string> LoadingFilePaths;
	return LoadPackageInternal(FilePath, LoadingFilePaths);
}

namespace
{

template <typename TResource>
[[nodiscard]] FObjectRef NewTypedResource(
	FGCSystem& GC,
	FResourceSystem& Resources,
	UPackage& Package,
	const std::string& ObjectName,
	EResourceType Type,
	const std::string& ImportSource)
{
	FObjectRef Ref = GC.NewObject<TResource>(&Package, ObjectName, Type, ImportSource);
	if (!Ref || !Resources.RegisterOwnedResource(Package, Ref))
	{
		return {};
	}
	return Ref;
}

[[nodiscard]] FObjectRef CreateResourceFromParsed(
	FResourceSystem& Manager,
	FGCSystem& GC,
	UPackage& Package,
	const ResourceCasset::FCassetParsedObject& Entry)
{
	if (Entry.Name.empty())
	{
		MAHO_CORE_ERROR("FResourceSystem: empty object name in casset");
		return {};
	}

	FObjectRef Created;
	switch (Entry.Type)
	{
	case EResourceType::Texture:
	case EResourceType::Texture2D:
		Created = NewTypedResource<UTexture2D>(GC, Manager, Package, Entry.Name, EResourceType::Texture2D, Entry.ImportSource);
		break;
	case EResourceType::Texture3D:
		Created = NewTypedResource<UTexture3D>(GC, Manager, Package, Entry.Name, Entry.Type, Entry.ImportSource);
		break;
	case EResourceType::TextureCube:
		Created = NewTypedResource<UTextureCube>(GC, Manager, Package, Entry.Name, Entry.Type, Entry.ImportSource);
		break;
	case EResourceType::TextureCubeArray:
		Created = NewTypedResource<UTextureCubeArray>(GC, Manager, Package, Entry.Name, Entry.Type, Entry.ImportSource);
		break;
	case EResourceType::Texture2DArray:
		Created = NewTypedResource<UTexture2DArray>(GC, Manager, Package, Entry.Name, Entry.Type, Entry.ImportSource);
		break;
	case EResourceType::Mesh:
		Created = NewTypedResource<UStaticMesh>(GC, Manager, Package, Entry.Name, Entry.Type, Entry.ImportSource);
		break;
	case EResourceType::Material:
		Created = NewTypedResource<UMaterial>(GC, Manager, Package, Entry.Name, Entry.Type, Entry.ImportSource);
		break;
	case EResourceType::Skeleton:
		Created = NewTypedResource<USkeleton>(GC, Manager, Package, Entry.Name, Entry.Type, Entry.ImportSource);
		break;
	case EResourceType::Animation:
		Created = NewTypedResource<UAnimation>(GC, Manager, Package, Entry.Name, Entry.Type, Entry.ImportSource);
		break;
	case EResourceType::AnimationGraph:
		Created = NewTypedResource<UAnimationGraph>(GC, Manager, Package, Entry.Name, Entry.Type, Entry.ImportSource);
		break;
	case EResourceType::Prefab:
		Created = NewTypedResource<UPrefab>(GC, Manager, Package, Entry.Name, Entry.Type, Entry.ImportSource);
		break;
	default:
		MAHO_CORE_ERROR(
			"FResourceSystem: unsupported casset type {} for '{}'",
			static_cast<int>(Entry.Type),
			Entry.Name);
		return {};
	}

	UResource* Resource = Created.Cast<UResource>();
	if (!Resource)
	{
		return {};
	}
	if ((Entry.RecordFlags & ResourceCasset::kRecordHasCpu) != 0)
	{
		if (!ResourceCasset::ApplyCpuPayload(*Resource, Entry.CpuBytes))
		{
			Manager.UnregisterResource(Resource);
			return {};
		}
	}
	else
	{
		Resource->MarkCpuReady();
		Resource->ClearDirty();
	}
	return Created;
}

} // namespace

FObjectRef FResourceSystem::LoadPackageInternal(
	const std::string& FilePath,
	std::unordered_set<std::string>& LoadingFilePaths)
{
	if (!IsInitialized())
	{
		MAHO_CORE_ERROR("FResourceSystem::LoadPackage: not initialized");
		return {};
	}

	if (!bAcceptingNewWork)
	{
		MAHO_CORE_ERROR("FResourceSystem::LoadPackage: refused during exit");
		return {};
	}

	if (FilePath.empty())
	{
		MAHO_CORE_ERROR("FResourceSystem::LoadPackage: empty file path");
		return {};
	}

	const std::string NormalizedFile = NormalizeSourcePath(FilePath);
	if (LoadingFilePaths.find(NormalizedFile) != LoadingFilePaths.end())
	{
		MAHO_CORE_ERROR(
			"FResourceSystem::LoadPackage: cycle detected at '{}'",
			FilePath);
		return {};
	}
	LoadingFilePaths.insert(NormalizedFile);

	std::ifstream In(PathFromUtf8(FilePath), std::ios::binary | std::ios::ate);
	if (!In)
	{
		MAHO_CORE_ERROR("FResourceSystem::LoadPackage: open failed '{}'", FilePath);
		LoadingFilePaths.erase(NormalizedFile);
		return {};
	}
	const std::streamoff Size = In.tellg();
	if (Size < 0)
	{
		LoadingFilePaths.erase(NormalizedFile);
		return {};
	}
	In.seekg(0, std::ios::beg);
	std::vector<std::uint8_t> Bytes(static_cast<std::size_t>(Size));
	if (Size > 0 && !In.read(reinterpret_cast<char*>(Bytes.data()), Size))
	{
		MAHO_CORE_ERROR("FResourceSystem::LoadPackage: read failed '{}'", FilePath);
		LoadingFilePaths.erase(NormalizedFile);
		return {};
	}

	return LoadPackageFromBinary(FilePath, Bytes.data(), Bytes.size(), LoadingFilePaths);
}

FObjectRef FResourceSystem::LoadPackageFromBinary(
	const std::string& FilePath,
	const std::uint8_t* FileBytes,
	std::size_t FileSize,
	std::unordered_set<std::string>& LoadingFilePaths)
{
	const std::string NormalizedFile = NormalizeSourcePath(FilePath);
	if (LoadingFilePaths.find(NormalizedFile) == LoadingFilePaths.end())
	{
		LoadingFilePaths.insert(NormalizedFile);
	}

	ResourceCasset::FCassetParsedPackage Parsed;
	if (!ResourceCasset::DecodePackageFile(FileBytes, FileSize, Parsed))
	{
		LoadingFilePaths.erase(NormalizedFile);
		return {};
	}

	for (const ResourceCasset::FCassetDependency& Dep : Parsed.Dependencies)
	{
		const std::string DepName = NormalizePackageName(Dep.PackageName);
		if (!DepName.empty())
		{
			FGCSystem* GC = Detail::GetGCSystem();
			if (GC && GC->FindPackage(DepName))
			{
				continue;
			}
		}
		if (Dep.FilePath.empty())
		{
			MAHO_CORE_WARN(
				"FResourceSystem::LoadPackage: dependency '{}' has empty file — skip",
				DepName.empty() ? "<unnamed>" : DepName);
			continue;
		}
		if (!LoadPackageInternal(Dep.FilePath, LoadingFilePaths))
		{
			MAHO_CORE_ERROR(
				"FResourceSystem::LoadPackage: failed loading dependency '{}' from '{}'",
				DepName.empty() ? Dep.FilePath : DepName,
				Dep.FilePath);
			LoadingFilePaths.erase(NormalizedFile);
			return {};
		}
	}

	std::string PackageName = NormalizePackageName(Parsed.Name);
	if (PackageName.empty())
	{
		PackageName = FilePath;
	}

	FGCSystem* GC = Detail::GetGCSystem();
	if (FObjectRef Existing = GC ? GC->FindPackage(PackageName) : FObjectRef{})
	{
		MAHO_CORE_WARN(
			"FResourceSystem::LoadPackage: '{}' already loaded — returning existing",
			PackageName);
		LoadingFilePaths.erase(NormalizedFile);
		return Existing;
	}

	FObjectRef PackageRef = GC
		? GC->NewObject<UPackage>(PackageName, EPackageFlags::Persistent)
		: FObjectRef{};
	UPackage* Raw = PackageRef.Cast<UPackage>();
	if (!Raw || !GC)
	{
		MAHO_CORE_ERROR("FResourceSystem::LoadPackage: NewObject<UPackage> failed");
		LoadingFilePaths.erase(NormalizedFile);
		return {};
	}

	Raw->PackageFlags = static_cast<EPackageFlags>(Parsed.PackageFlags);
	if (!HasAnyPackageFlags(Raw->PackageFlags, EPackageFlags::Transient)
		&& !HasAnyPackageFlags(Raw->PackageFlags, EPackageFlags::Persistent))
	{
		Raw->PackageFlags |= EPackageFlags::Persistent;
	}
	Raw->AddPackageFlags(EPackageFlags::Persistent);
	Raw->ClearPackageFlags(EPackageFlags::Transient);
	Raw->SetFilePath(FilePath);

	struct FPendingObjectLinks
	{
		FObjectRef Object;
		std::vector<std::string> SoftPaths;
	};
	std::vector<FPendingObjectLinks> PendingLinks;

	for (const ResourceCasset::FCassetParsedObject& Entry : Parsed.Objects)
	{
		FObjectRef Created = CreateResourceFromParsed(*this, *GC, *Raw, Entry);
		if (!Created)
		{
			MAHO_CORE_WARN(
				"FResourceSystem::LoadPackage: failed to create '{}' in '{}'",
				Entry.Name,
				PackageName);
			continue;
		}
		FPendingObjectLinks Links;
		Links.Object = Created;
		Links.SoftPaths = Entry.Refs;
		PendingLinks.push_back(std::move(Links));
	}

	for (FPendingObjectLinks& Links : PendingLinks)
	{
		if (Links.SoftPaths.empty() || !Links.Object)
		{
			continue;
		}
		std::vector<UObject*> Resolved;
		Resolved.reserve(Links.SoftPaths.size());
		for (const std::string& SoftPath : Links.SoftPaths)
		{
			if (SoftPath.empty())
			{
				Resolved.push_back(nullptr);
				continue;
			}
			FObjectRef ResolvedRef = ResolveObjectPath(SoftPath);
			if (!ResolvedRef)
			{
				MAHO_CORE_WARN(
					"FResourceSystem::LoadPackage: unresolved ref '{}' in '{}'",
					SoftPath,
					PackageName);
				Resolved.push_back(nullptr);
				continue;
			}
			Resolved.push_back(ResolvedRef.Get());
		}
		Links.Object->SetReferencedObjects(Resolved);
	}

	MAHO_CORE_INFO(
		"Loaded package '{}' ({} objects) from '{}'",
		Raw->GetName(),
		Raw->GetObjectCount(),
		FilePath);
	LoadingFilePaths.erase(NormalizedFile);
	return PackageRef;
}

EResourceLoadState FResourceSystem::GetLoadState(const FSoftObjectPath& SoftPath) const
{
	if (!SoftPath.IsValid())
	{
		return EResourceLoadState::Invalid;
	}

	const std::string Key = SoftPathKey(SoftPath);
	const auto PendingIt = PendingIO.find(Key);
	if (PendingIt != PendingIO.end())
	{
		const FTransferHandle Handle = PendingIt->second.Handle;
		if (Handle.HasFailed() || !Handle.IsValid())
		{
			return EResourceLoadState::Failed;
		}
		return EResourceLoadState::Pending;
	}

	if (FObjectRef Resolved = SoftPath.Resolve())
	{
		if (const UResource* Resource = Resolved.Cast<UResource>())
		{
			return Resource->GetLoadState();
		}
		return EResourceLoadState::Ready;
	}

	FGCSystem* GC = Detail::GetGCSystem();
	if (GC && GC->FindPackage(SoftPath.GetPackageName()))
	{
		return EResourceLoadState::Ready;
	}

	return EResourceLoadState::Invalid;
}

bool FResourceSystem::IsReady(const FSoftObjectPath& SoftPath) const
{
	return GetLoadState(SoftPath) == EResourceLoadState::Ready;
}

void FResourceSystem::Flush(const FSoftObjectPath& SoftPath)
{
	if (!IsInitialized() || !SoftPath.IsValid())
	{
		return;
	}

	const std::string Key = SoftPathKey(SoftPath);
	const auto It = PendingIO.find(Key);
	if (It != PendingIO.end() && HasActiveServer())
	{
		Server->Flush(It->second.Handle);
	}

	while (PendingIO.find(Key) != PendingIO.end())
	{
		ProcessReadyIO();
		if (PendingIO.find(Key) == PendingIO.end())
		{
			break;
		}
		const auto Still = PendingIO.find(Key);
		if (Still != PendingIO.end() && Still->second.Handle.IsInProgress())
		{
			Server->Flush(Still->second.Handle);
		}
		else if (Still != PendingIO.end())
		{
			ProcessReadyIO();
		}
		else
		{
			break;
		}
	}
}

void FResourceSystem::FlushAll()
{
	if (!IsInitialized())
	{
		return;
	}

	for (const auto& Pair : PendingIO)
	{
		Server->Flush(Pair.second.Handle);
	}

	Server->FThreadedServer::Flush();
	while (!PendingIO.empty())
	{
		ProcessReadyIO();
	}
}

namespace Detail
{

FResourceSystem* GetResourceSystem()
{
	if (!GApp)
	{
		return nullptr;
	}
	return GApp->GetExtension<FResourceSystem>();
}

} // namespace Detail

} // namespace Maho
