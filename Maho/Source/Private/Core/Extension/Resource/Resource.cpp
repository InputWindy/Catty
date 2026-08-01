#include "ResourceIO.h"

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
#include <utility>

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

bool FResourceSystem::CanImportSourcePath(const std::string& SourcePath) const
{
	if (SourcePath.empty())
	{
		return false;
	}
	for (const auto& Importer : Importers)
	{
		if (!Importer || Importer->GetType() == EResourceType::Raw)
		{
			continue;
		}
		if (Importer->MatchesSourcePath(SourcePath))
		{
			return true;
		}
	}
	return false;
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
	FlushAll();

	ImportPins.clear();
	PendingImports.clear();

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
		&& PendingImports.empty()
		&& ImportPins.empty()
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
		ProcessReadyImports();
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

	Importers.clear();
	Exporters.clear();

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

	for (auto It = PendingImports.begin(); It != PendingImports.end();)
	{
		if (It->second.Resource.GetRaw() == Resource)
		{
			const std::uint64_t LoadId = It->second.LoadId;
			ReleaseLoadId(LoadId);
			ImportPins.erase(LoadId);
			It = PendingImports.erase(It);
		}
		else
		{
			++It;
		}
	}
}

void FResourceSystem::ProcessReadyImports()
{
	if (!HasActiveServer() || PendingImports.empty())
	{
		return;
	}

	std::vector<std::uint64_t> ReadyIds;
	ReadyIds.reserve(PendingImports.size());
	for (const auto& Pair : PendingImports)
	{
		const FResourceId Id{Pair.second.LoadId};
		const EResourceLoadState State = Server->GetLoadState(Id);
		if (State != EResourceLoadState::Pending)
		{
			ReadyIds.push_back(Pair.first);
		}
	}

	for (const std::uint64_t IdValue : ReadyIds)
	{
		const auto It = PendingImports.find(IdValue);
		if (It == PendingImports.end())
		{
			continue;
		}

		FPendingImport Pending = std::move(It->second);
		PendingImports.erase(It);

		// Keep ImportPins[LoadId] until this iteration finishes so Apply/Abort cannot race GC.
		const auto Unpin = [this, LoadId = Pending.LoadId]()
		{
			ImportPins.erase(LoadId);
		};

		UResource* Resource = Pending.Resource.Cast<UResource>();
		const FResourceId Id{Pending.LoadId};
		const EResourceLoadState BulkState = Server->GetLoadState(Id);

		if (!Resource)
		{
			MAHO_CORE_ERROR(
				"FResourceSystem::ProcessReadyImports: lost Resource pin for LoadId={} — scrubbing",
				Pending.LoadId);
			ReleaseLoadId(Pending.LoadId);
			Unpin();
			continue;
		}

		if (BulkState == EResourceLoadState::Failed || BulkState == EResourceLoadState::Invalid)
		{
			Resource->SetLoadState(EResourceLoadState::Failed);
			AbortFailedImport(*Resource);
			ReleaseLoadId(Pending.LoadId);
			Unpin();
			continue;
		}

		FResourceBulkData Bulk;
		if (!TakeBulkData(Pending.LoadId, Bulk))
		{
			MAHO_CORE_ERROR(
				"FResourceSystem::ProcessReadyImports: TakeBulkData failed for '{}'",
				Resource->GetSourcePath());
			Resource->SetLoadState(EResourceLoadState::Failed);
			AbortFailedImport(*Resource);
			ReleaseLoadId(Pending.LoadId);
			Unpin();
			continue;
		}

		const bool bOk = Pending.Importer
			&& Pending.Importer->ApplyBulkData(Pending.Config, Bulk, Pending.Resource);
		Resource->SetLoadState(bOk ? EResourceLoadState::Ready : EResourceLoadState::Failed);
		ReleaseLoadId(Pending.LoadId);

		if (!bOk)
		{
			MAHO_CORE_ERROR(
				"FResourceSystem::ProcessReadyImports: Importer failed for '{}'",
				Resource->GetSourcePath());
			AbortFailedImport(*Resource);
		}
		Unpin();
	}
}

void FResourceSystem::RegisterImporter(std::unique_ptr<IResourceImporter> Importer)
{
	if (Importer)
	{
		Importers.push_back(std::move(Importer));
	}
}

void FResourceSystem::RegisterExporter(std::unique_ptr<IResourceExporter> Exporter)
{
	if (Exporter)
	{
		Exporters.push_back(std::move(Exporter));
	}
}

void FResourceSystem::ClearImportersAndExporters()
{
	Importers.clear();
	Exporters.clear();
}

IResourceImporter* FResourceSystem::FindImporter(const FResourceImportConfig& Config) const
{
	if (Config.TypeHint != EResourceType::Unknown)
	{
		for (const auto& Importer : Importers)
		{
			if (Importer && Importer->GetType() == Config.TypeHint)
			{
				return Importer.get();
			}
		}
	}

	IResourceImporter* Fallback = nullptr;
	for (const auto& Importer : Importers)
	{
		if (!Importer)
		{
			continue;
		}

		if (Importer->GetType() == EResourceType::Raw)
		{
			Fallback = Importer.get();
			continue;
		}

		if (Importer->MatchesSourcePath(Config.SourcePath))
		{
			return Importer.get();
		}
	}

	return Fallback;
}

IResourceExporter* FResourceSystem::FindExporter(const FObjectRef& Resource) const
{
	IResourceExporter* Fallback = nullptr;
	for (const auto& Exporter : Exporters)
	{
		if (!Exporter || !Exporter->CanExport(Resource))
		{
			continue;
		}

		if (Exporter->GetType() == EResourceType::Raw)
		{
			Fallback = Exporter.get();
			continue;
		}

		return Exporter.get();
	}

	return Fallback;
}

bool FResourceSystem::HasActiveServer() const
{
	return Server && Server->IsInitialized();
}

std::uint64_t FResourceSystem::RequestLoadId(const std::string& SourcePath)
{
	if (!HasActiveServer())
	{
		return 0;
	}
	return Server->RequestLoad(SourcePath).Value;
}

void FResourceSystem::ReleaseLoadId(std::uint64_t LoadId)
{
	if (HasActiveServer() && LoadId != 0)
	{
		Server->Release(FResourceId{LoadId});
	}
}

bool FResourceSystem::TakeBulkData(std::uint64_t LoadId, FResourceBulkData& OutBulk)
{
	if (!HasActiveServer() || LoadId == 0)
	{
		return false;
	}
	return Server->TryTakeBulkData(FResourceId{LoadId}, OutBulk);
}

FObjectRef FResourceSystem::KickImport(FResourceImportConfig Config)
{
	IResourceImporter* Importer = FindImporter(Config);
	if (!Importer)
	{
		MAHO_CORE_ERROR(
			"FResourceSystem::KickImport: no importer for '{}' (type hint {})",
			Config.SourcePath,
			static_cast<int>(Config.TypeHint));
		return {};
	}

	return Importer->Import(*this, std::move(Config));
}

bool FResourceSystem::KickExport(FResourceExportConfig Config, const FObjectRef& Resource)
{
	IResourceExporter* Exporter = FindExporter(Resource);
	if (!Exporter)
	{
		MAHO_CORE_ERROR("FResourceSystem::KickExport: no exporter for resource");
		return false;
	}

	return Exporter->Export(std::move(Config), Resource);
}

FObjectRef FResourceSystem::LoadResourceIntoPackage(
	const FObjectRef& Package,
	std::string ObjectName,
	std::string SourcePath,
	EResourceType Type)
{
	FResourceImportConfig Config;
	Config.Package = Package;
	Config.ObjectName = std::move(ObjectName);
	Config.SourcePath = std::move(SourcePath);
	Config.TypeHint = Type;
	return KickImport(std::move(Config));
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

	FGCSystem* GC = Detail::GetGCSystem();
	if (GC)
	{
		if (FObjectRef Existing = GC->FindObject(SoftPath.GetPackageName(), SoftPath.GetAssetName()))
		{
			return Existing;
		}
	}

	if (!GC || !GC->FindPackage(SoftPath.GetPackageName()))
	{
		const std::string Filename = FPaths::ConvertPackageNameToFilename(SoftPath.GetPackageName());
		if (Filename.empty())
		{
			MAHO_CORE_ERROR(
				"FResourceSystem::TryLoad: no mount mapping for '{}'",
				SoftPath.GetPackageName());
			return {};
		}

		if (!LoadPackage(Filename))
		{
			MAHO_CORE_ERROR(
				"FResourceSystem::TryLoad: LoadPackage failed for '{}' ('{}')",
				SoftPath.GetPackageName(),
				Filename);
			return {};
		}
	}

	GC = Detail::GetGCSystem();
	return GC ? GC->FindObject(SoftPath.GetPackageName(), SoftPath.GetAssetName()) : FObjectRef{};
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

	FJsonValue Root = FJsonValue::Object();
	if (!PackageObj.Serialize(Root))
	{
		MAHO_CORE_ERROR("FResourceSystem::SavePackage: Serialize failed for '{}'", PackageObj.GetName());
		SavingPackageNames.erase(PackageKey);
		return false;
	}

	if (bSaveDependencies)
	{
		if (Root.HasField("dependencies") && Root.GetField("dependencies").IsArray())
		{
			const FJsonValue Deps = Root.GetField("dependencies");
			const std::size_t DepCount = Deps.GetArraySize();
			for (std::size_t Index = 0; Index < DepCount; ++Index)
			{
				const FJsonValue DepEntry = Deps.GetElement(Index);
				if (!DepEntry.IsObject())
				{
					continue;
				}

				const std::string DepName = NormalizePackageName(DepEntry.GetField("name").AsString());
				std::string DepFile = DepEntry.GetField("file").AsString();
				if (DepName.empty())
				{
					continue;
				}

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

		// Refresh JSON so dependency file paths set during recursive save are recorded.
		Root = FJsonValue::Object();
		if (!PackageObj.Serialize(Root))
		{
			MAHO_CORE_ERROR("FResourceSystem::SavePackage: Serialize failed for '{}'", PackageObj.GetName());
			SavingPackageNames.erase(PackageKey);
			return false;
		}
	}

	FJsonDocument Doc;
	Doc.SetRoot(std::move(Root));
	if (!Doc.SaveToFile(OutPath, bPretty))
	{
		MAHO_CORE_ERROR("FResourceSystem::SavePackage: write failed '{}'", OutPath);
		SavingPackageNames.erase(PackageKey);
		return false;
	}

	MAHO_CORE_INFO(
		"Saved package '{}' ({} objects) -> '{}'",
		PackageObj.GetName(),
		PackageObj.GetObjectCount(),
		OutPath);
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

	FJsonDocument Doc;
	if (!Doc.LoadFromFile(FilePath))
	{
		MAHO_CORE_ERROR("FResourceSystem::LoadPackage: read failed '{}'", FilePath);
		LoadingFilePaths.erase(NormalizedFile);
		return {};
	}

	const FJsonValue& Root = Doc.GetRoot();
	if (!Root.IsObject())
	{
		MAHO_CORE_ERROR("FResourceSystem::LoadPackage: root is not an object '{}'", FilePath);
		LoadingFilePaths.erase(NormalizedFile);
		return {};
	}

	// Load dependency packages first (depth-first).
	if (Root.HasField("dependencies") && Root.GetField("dependencies").IsArray())
	{
		const FJsonValue Deps = Root.GetField("dependencies");
		const std::size_t DepCount = Deps.GetArraySize();
		for (std::size_t Index = 0; Index < DepCount; ++Index)
		{
			const FJsonValue DepEntry = Deps.GetElement(Index);
			if (!DepEntry.IsObject())
			{
				continue;
			}

			const std::string DepName = NormalizePackageName(DepEntry.GetField("name").AsString());
			const std::string DepFile = DepEntry.GetField("file").AsString();

			if (!DepName.empty())
			{
				FGCSystem* GC = Detail::GetGCSystem();
				if (GC && GC->FindPackage(DepName))
				{
					continue;
				}
			}

			if (DepFile.empty())
			{
				MAHO_CORE_WARN(
					"FResourceSystem::LoadPackage: dependency '{}' has empty file — skip",
					DepName.empty() ? "<unnamed>" : DepName);
				continue;
			}

			if (!LoadPackageInternal(DepFile, LoadingFilePaths))
			{
				MAHO_CORE_ERROR(
					"FResourceSystem::LoadPackage: failed loading dependency '{}' from '{}'",
					DepName.empty() ? DepFile : DepName,
					DepFile);
				LoadingFilePaths.erase(NormalizedFile);
				return {};
			}
		}
	}

	std::string PackageName = NormalizePackageName(Root.GetField("name").AsString());
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
	if (!Raw)
	{
		MAHO_CORE_ERROR("FResourceSystem::LoadPackage: NewObject<UPackage> failed");
		LoadingFilePaths.erase(NormalizedFile);
		return {};
	}

	if (!Raw->Deserialize(Root))
	{
		MAHO_CORE_ERROR("FResourceSystem::LoadPackage: Deserialize failed '{}'", FilePath);
		LoadingFilePaths.erase(NormalizedFile);
		// Drop the only Ref so GC can finalize; otherwise FindPackage keeps returning this shell.
		PackageRef = {};
		GC->CollectGarbage();
		GC->PurgePendingKill();
		return {};
	}

	Raw->AddPackageFlags(EPackageFlags::Persistent);
	Raw->ClearPackageFlags(EPackageFlags::Transient);
	Raw->SetFilePath(FilePath);

	FJsonValue ObjectField = FJsonValue::Null();
	if (Root.HasField("objects"))
	{
		ObjectField = Root.GetField("objects");
	}
	else if (Root.HasField("exports"))
	{
		ObjectField = Root.GetField("exports");
	}
	else if (Root.HasField("resources"))
	{
		ObjectField = Root.GetField("resources");
	}

	struct FPendingObjectLinks
	{
		FObjectRef Object;
		std::vector<std::string> SoftPaths;
	};
	std::vector<FPendingObjectLinks> PendingLinks;

	if (ObjectField.IsArray())
	{
		const std::size_t Count = ObjectField.GetArraySize();
		for (std::size_t Index = 0; Index < Count; ++Index)
		{
			const FJsonValue Entry = ObjectField.GetElement(Index);
			if (!Entry.IsObject())
			{
				continue;
			}

			std::string ObjectName = Entry.GetField("name").AsString();
			std::string SourcePath = Entry.HasField("source")
				? Entry.GetField("source").AsString()
				: Entry.GetField("path").AsString();

			if (ObjectName.empty() && !SourcePath.empty())
			{
				ObjectName = MakeObjectNameFromSource(SourcePath);
			}

			FObjectRef Created;
			const std::string ClassName = Entry.GetField("class").AsString("Resource");

			EResourceType Type = EResourceType::Unknown;
			if (Entry.HasField("type"))
			{
				Type = ResourceTypeFromString(Entry.GetField("type").AsString());
			}
			if (Type == EResourceType::Unknown
				&& !TryResourceTypeFromClassName(ClassName.empty() ? "Resource" : ClassName, Type))
			{
				MAHO_CORE_WARN(
					"FResourceSystem::LoadPackage: unsupported class '{}' for '{}' — skip",
					ClassName,
					ObjectName);
				continue;
			}

			if (SourcePath.empty())
			{
				MAHO_CORE_WARN(
					"FResourceSystem::LoadPackage: object '{}' has no source — skip",
					ObjectName);
				continue;
			}

			Created = LoadResourceIntoPackage(PackageRef, ObjectName, SourcePath, Type);

			if (!Created)
			{
				MAHO_CORE_WARN(
					"FResourceSystem::LoadPackage: failed to create '{}' in '{}'",
					ObjectName,
					PackageName);
				continue;
			}

			FPendingObjectLinks Links;
			Links.Object = Created;
			if (Entry.HasField("refs") && Entry.GetField("refs").IsArray())
			{
				const FJsonValue RefArray = Entry.GetField("refs");
				const std::size_t RefCount = RefArray.GetArraySize();
				Links.SoftPaths.reserve(RefCount);
				for (std::size_t RefIndex = 0; RefIndex < RefCount; ++RefIndex)
				{
					Links.SoftPaths.push_back(RefArray.GetElement(RefIndex).AsString());
				}
			}
			PendingLinks.push_back(std::move(Links));
		}
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

EResourceLoadState FResourceSystem::GetLoadState(const FObjectRef& Object) const
{
	const UResource* Resource = Object.Cast<UResource>();
	if (!Resource)
	{
		return EResourceLoadState::Invalid;
	}

	return Resource->GetLoadState();
}

bool FResourceSystem::IsReady(const FObjectRef& Object) const
{
	return GetLoadState(Object) == EResourceLoadState::Ready;
}

void FResourceSystem::Flush(const FObjectRef& Object)
{
	if (!IsInitialized())
	{
		return;
	}

	UObject* ObjectPtr = Object.Get();
	if (!ObjectPtr)
	{
		return;
	}

	for (const auto& Pair : PendingImports)
	{
		if (Pair.second.Resource.Get() == ObjectPtr)
		{
			Server->Flush(FResourceId{Pair.second.LoadId});
			break;
		}
	}

	ProcessReadyImports();
}

void FResourceSystem::FlushAll()
{
	if (!IsInitialized())
	{
		return;
	}

	for (const auto& Pair : PendingImports)
	{
		Server->Flush(FResourceId{Pair.second.LoadId});
	}

	Server->FThreadedServer::Flush();
	ProcessReadyImports();
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
