#include "Core/Modules/ResourceIO.h"

#include <Core/Application/App.h>
#include <Core/Extension/GC.h>
#include <Core/Json.h>
#include <Core/System/Log.h>
#include <Core/System/Paths.h>
#include <Core/Object/Package.h>
#include <Core/Object/SoftObjectPath.h>
#include "Core/Modules/ResourceServer.h"

#include <ResourceTypes.gen.h>

#include <cctype>
#include <utility>

namespace Catty
{

FResourceManager::FResourceManager()
	: Server(std::make_unique<FResourceServer>())
{
}

FResourceManager::~FResourceManager()
{
	Shutdown();
}

bool FResourceManager::IsInitialized() const
{
	return Server && Server->IsInitialized();
}

std::string FResourceManager::NormalizePackageName(std::string Name)
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

std::string FResourceManager::NormalizeSourcePath(std::string Path)
{
	for (char& Ch : Path)
	{
		if (Ch == '\\')
		{
			Ch = '/';
		}
#if defined(_WIN32)
		Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(Ch)));
#endif
	}

	while (Path.size() >= 2 && Path[0] == '.' && Path[1] == '/')
	{
		Path.erase(0, 2);
	}

	return Path;
}

std::string FResourceManager::MakeObjectNameFromSource(const std::string& SourcePath)
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

bool FResourceManager::Initialize()
{
	if (IsInitialized())
	{
		return true;
	}

	FGC* GC = Detail::GetGC();
	if (!GC || !GC->IsInitialized())
	{
		CATTY_CORE_ERROR("FResourceManager::Initialize: FGC must be initialized first");
		return false;
	}

	if (!Server->Initialize())
	{
		CATTY_CORE_ERROR("FResourceManager::Initialize: FResourceServer failed");
		return false;
	}

	RegisterGeneratedResourceTypes(*this, *GC);

	bAcceptingNewWork = true;
	CATTY_CORE_INFO("ResourceManager initialized");
	return true;
}

bool FResourceManager::RegisterResource(const FObjectRef& Resource)
{
	if (!bAcceptingNewWork)
	{
		CATTY_CORE_ERROR("FResourceManager::RegisterResource: refused during exit");
		return false;
	}

	UResource* ResourcePtr = Resource.Cast<UResource>();
	if (!ResourcePtr)
	{
		CATTY_CORE_ERROR("FResourceManager::RegisterResource: Ref is not an UResource");
		return false;
	}

	const std::string CatalogKey = MakeResourceCatalogKey(*ResourcePtr);
	if (CatalogKey.empty())
	{
		CATTY_CORE_ERROR(
			"FResourceManager::RegisterResource: empty catalog key for '{}'",
			ResourcePtr->GetName());
		return false;
	}

	const auto Existing = Resources.find(CatalogKey);
	if (Existing != Resources.end() && Existing->second && Existing->second.Get() != ResourcePtr)
	{
		CATTY_CORE_ERROR(
			"FResourceManager::RegisterResource: '{}' already registered to another object",
			CatalogKey);
		return false;
	}

	Resources[CatalogKey] = Resource;
	return true;
}

bool FResourceManager::UnregisterResource(UObject* Resource)
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
	return true;
}

bool FResourceManager::UnregisterResource(const FObjectRef& Resource)
{
	return UnregisterResource(Resource.Get());
}

std::string FResourceManager::MakeResourceCatalogKey(const UResource& Resource)
{
	return NormalizeResourceVirtualPath(Resource.GetPathName());
}

std::string FResourceManager::NormalizeResourceVirtualPath(const std::string& VirtualPath)
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

void FResourceManager::UnregisterResourcesInPackage(const std::string& PackageName)
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

void FResourceManager::PrepareForExit()
{
	if (!IsInitialized())
	{
		return;
	}

	bAcceptingNewWork = false;
	FlushAll();

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

bool FResourceManager::IsIdle() const
{
	if (!IsInitialized())
	{
		return true;
	}
	return !bAcceptingNewWork
		&& Resources.empty()
		&& PendingImports.empty()
		&& !Server->HasPendingLoads();
}

bool FResourceManager::ExecuteStage(EEngineStage Stage)
{
	switch (Stage)
	{
	case EEngineStage::Init:
		if (!Initialize())
		{
			CATTY_CORE_ERROR("FResourceManager: Initialize failed");
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

void FResourceManager::Shutdown()
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

	CATTY_CORE_INFO("ResourceManager shut down");
}

void FResourceManager::CancelPendingImport(UObject* Resource)
{
	if (!Resource)
	{
		return;
	}

	for (auto It = PendingImports.begin(); It != PendingImports.end();)
	{
		if (It->second.Resource.Get() == Resource)
		{
			ReleaseLoadId(It->second.LoadId);
			It = PendingImports.erase(It);
		}
		else
		{
			++It;
		}
	}
}

void FResourceManager::ProcessReadyImports()
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

		UResource* Resource = Pending.Resource.Cast<UResource>();
		const FResourceId Id{Pending.LoadId};
		const EResourceLoadState BulkState = Server->GetLoadState(Id);

		if (!Resource)
		{
			ReleaseLoadId(Pending.LoadId);
			continue;
		}

		if (BulkState == EResourceLoadState::Failed || BulkState == EResourceLoadState::Invalid)
		{
			Resource->SetLoadState(EResourceLoadState::Failed);
			ReleaseLoadId(Pending.LoadId);
			continue;
		}

		FResourceBulkData Bulk;
		if (!TakeBulkData(Pending.LoadId, Bulk))
		{
			CATTY_CORE_ERROR(
				"FResourceManager::ProcessReadyImports: TakeBulkData failed for '{}'",
				Resource->GetSourcePath());
			Resource->SetLoadState(EResourceLoadState::Failed);
			ReleaseLoadId(Pending.LoadId);
			continue;
		}

		const bool bOk = Pending.Importer
			&& Pending.Importer->ApplyBulkData(Pending.Config, Bulk, Pending.Resource);
		Resource->SetLoadState(bOk ? EResourceLoadState::Ready : EResourceLoadState::Failed);
		ReleaseLoadId(Pending.LoadId);

		if (!bOk)
		{
			CATTY_CORE_ERROR(
				"FResourceManager::ProcessReadyImports: Importer failed for '{}'",
				Resource->GetSourcePath());
		}
	}
}

void FResourceManager::RegisterImporter(std::unique_ptr<IResourceImporter> Importer)
{
	if (Importer)
	{
		Importers.push_back(std::move(Importer));
	}
}

void FResourceManager::RegisterExporter(std::unique_ptr<IResourceExporter> Exporter)
{
	if (Exporter)
	{
		Exporters.push_back(std::move(Exporter));
	}
}

void FResourceManager::ClearImportersAndExporters()
{
	Importers.clear();
	Exporters.clear();
}

IResourceImporter* FResourceManager::FindImporter(const FResourceImportConfig& Config) const
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

IResourceExporter* FResourceManager::FindExporter(const FObjectRef& Resource) const
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

bool FResourceManager::HasActiveServer() const
{
	return Server && Server->IsInitialized();
}

std::uint64_t FResourceManager::RequestLoadId(const std::string& SourcePath)
{
	if (!HasActiveServer())
	{
		return 0;
	}
	return Server->RequestLoad(SourcePath).Value;
}

void FResourceManager::ReleaseLoadId(std::uint64_t LoadId)
{
	if (HasActiveServer() && LoadId != 0)
	{
		Server->Release(FResourceId{LoadId});
	}
}

bool FResourceManager::TakeBulkData(std::uint64_t LoadId, FResourceBulkData& OutBulk)
{
	if (!HasActiveServer() || LoadId == 0)
	{
		return false;
	}
	return Server->TryTakeBulkData(FResourceId{LoadId}, OutBulk);
}

FObjectRef FResourceManager::KickImport(FResourceImportConfig Config)
{
	IResourceImporter* Importer = FindImporter(Config);
	if (!Importer)
	{
		CATTY_CORE_ERROR(
			"FResourceManager::KickImport: no importer for '{}' (type hint {})",
			Config.SourcePath,
			static_cast<int>(Config.TypeHint));
		return {};
	}

	return Importer->Import(*this, std::move(Config));
}

bool FResourceManager::KickExport(FResourceExportConfig Config, const FObjectRef& Resource)
{
	IResourceExporter* Exporter = FindExporter(Resource);
	if (!Exporter)
	{
		CATTY_CORE_ERROR("FResourceManager::KickExport: no exporter for resource");
		return false;
	}

	return Exporter->Export(std::move(Config), Resource);
}

FObjectRef FResourceManager::LoadResourceIntoPackage(
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

bool FResourceManager::UnloadResource(const std::string& VirtualPath)
{
	FGC* GC = Detail::GetGC();
	FObjectRef Found = GC ? GC->FindObject(VirtualPath) : FObjectRef{};
	UResource* ResourcePtr = Found.Cast<UResource>();
	if (!ResourcePtr)
	{
		return false;
	}

	return UnregisterResource(ResourcePtr);
}

bool FResourceManager::UnloadResource(const FObjectRef& Resource)
{
	UResource* ResourcePtr = Resource.Cast<UResource>();
	if (!ResourcePtr)
	{
		return false;
	}

	return UnregisterResource(ResourcePtr);
}

FObjectRef FResourceManager::TryLoad(const FSoftObjectPath& SoftPath)
{
	if (!SoftPath.IsValid())
	{
		return {};
	}

	FGC* GC = Detail::GetGC();
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
			CATTY_CORE_ERROR(
				"FResourceManager::TryLoad: no mount mapping for '{}'",
				SoftPath.GetPackageName());
			return {};
		}

		if (!LoadPackage(Filename))
		{
			CATTY_CORE_ERROR(
				"FResourceManager::TryLoad: LoadPackage failed for '{}' ('{}')",
				SoftPath.GetPackageName(),
				Filename);
			return {};
		}
	}

	GC = Detail::GetGC();
	return GC ? GC->FindObject(SoftPath.GetPackageName(), SoftPath.GetAssetName()) : FObjectRef{};
}

FObjectRef FResourceManager::TryLoad(const std::string& SoftPathString)
{
	FSoftObjectPath SoftPath;
	if (!SoftPath.TrySetPath(SoftPathString) || !SoftPath.IsValid())
	{
		return {};
	}
	return TryLoad(SoftPath);
}

bool FResourceManager::SavePackage(
	const FObjectRef& Package,
	const std::string& FilePath,
	bool bPretty,
	bool bSaveDependencies)
{
	std::unordered_set<std::string> SavingPackageNames;
	return SavePackageInternal(Package, FilePath, bPretty, bSaveDependencies, SavingPackageNames);
}

bool FResourceManager::SavePackageInternal(
	const FObjectRef& Package,
	const std::string& FilePath,
	bool bPretty,
	bool bSaveDependencies,
	std::unordered_set<std::string>& SavingPackageNames)
{
	if (!IsInitialized())
	{
		CATTY_CORE_ERROR("FResourceManager::SavePackage: not initialized");
		return false;
	}

	if (!Package)
	{
		CATTY_CORE_ERROR("FResourceManager::SavePackage: invalid Package");
		return false;
	}

	UPackage* PackagePtr = Package.Cast<UPackage>();
	if (!PackagePtr)
	{
		CATTY_CORE_ERROR("FResourceManager::SavePackage: Ref is not an UPackage");
		return false;
	}

	UPackage& PackageObj = *PackagePtr;

	const std::string PackageKey = NormalizePackageName(PackageObj.GetName());
	if (SavingPackageNames.find(PackageKey) != SavingPackageNames.end())
	{
		CATTY_CORE_ERROR(
			"FResourceManager::SavePackage: cycle detected while saving '{}'",
			PackageKey);
		return false;
	}
	SavingPackageNames.insert(PackageKey);

	const std::string OutPath = FilePath.empty() ? PackageObj.GetFilePath() : FilePath;
	if (OutPath.empty())
	{
		CATTY_CORE_ERROR("FResourceManager::SavePackage: empty file path for '{}'", PackageKey);
		SavingPackageNames.erase(PackageKey);
		return false;
	}

	if (!PackageObj.IsPersistent())
	{
		CATTY_CORE_ERROR(
			"FResourceManager::SavePackage: package '{}' is Transient — mark Persistent first",
			PackageObj.GetName());
		SavingPackageNames.erase(PackageKey);
		return false;
	}

	// Ensure FilePath is set before serializing so dependents can record it.
	PackageObj.SetFilePath(OutPath);

	FJsonValue Root = FJsonValue::Object();
	if (!PackageObj.Serialize(Root))
	{
		CATTY_CORE_ERROR("FResourceManager::SavePackage: Serialize failed for '{}'", PackageObj.GetName());
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

				FGC* GC = Detail::GetGC();
				FObjectRef DepPackage = GC ? GC->FindPackage(DepName) : FObjectRef{};
				UPackage* DepPtr = DepPackage.Cast<UPackage>();
				if (!DepPtr)
				{
					CATTY_CORE_ERROR(
						"FResourceManager::SavePackage: dependency '{}' not loaded",
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
					CATTY_CORE_ERROR(
						"FResourceManager::SavePackage: dependency '{}' has no file path",
						DepName);
					SavingPackageNames.erase(PackageKey);
					return false;
				}

				if (!SavePackageInternal(DepPackage, DepFile, bPretty, true, SavingPackageNames))
				{
					CATTY_CORE_ERROR(
						"FResourceManager::SavePackage: failed saving dependency '{}' for '{}'",
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
			CATTY_CORE_ERROR("FResourceManager::SavePackage: Serialize failed for '{}'", PackageObj.GetName());
			SavingPackageNames.erase(PackageKey);
			return false;
		}
	}

	FJsonDocument Doc;
	Doc.SetRoot(std::move(Root));
	if (!Doc.SaveToFile(OutPath, bPretty))
	{
		CATTY_CORE_ERROR("FResourceManager::SavePackage: write failed '{}'", OutPath);
		SavingPackageNames.erase(PackageKey);
		return false;
	}

	CATTY_CORE_INFO(
		"Saved package '{}' ({} objects) -> '{}'",
		PackageObj.GetName(),
		PackageObj.GetObjectCount(),
		OutPath);
	SavingPackageNames.erase(PackageKey);
	return true;
}

FObjectRef FResourceManager::ResolveObjectPath(const std::string& PathName) const
{
	FGC* GC = Detail::GetGC();
	return GC ? GC->FindObject(PathName) : FObjectRef{};
}

FObjectRef FResourceManager::LoadPackage(const std::string& FilePath)
{
	std::unordered_set<std::string> LoadingFilePaths;
	return LoadPackageInternal(FilePath, LoadingFilePaths);
}

FObjectRef FResourceManager::LoadPackageInternal(
	const std::string& FilePath,
	std::unordered_set<std::string>& LoadingFilePaths)
{
	if (!IsInitialized())
	{
		CATTY_CORE_ERROR("FResourceManager::LoadPackage: not initialized");
		return {};
	}

	if (!bAcceptingNewWork)
	{
		CATTY_CORE_ERROR("FResourceManager::LoadPackage: refused during exit");
		return {};
	}

	if (FilePath.empty())
	{
		CATTY_CORE_ERROR("FResourceManager::LoadPackage: empty file path");
		return {};
	}

	const std::string NormalizedFile = NormalizeSourcePath(FilePath);
	if (LoadingFilePaths.find(NormalizedFile) != LoadingFilePaths.end())
	{
		CATTY_CORE_ERROR(
			"FResourceManager::LoadPackage: cycle detected at '{}'",
			FilePath);
		return {};
	}
	LoadingFilePaths.insert(NormalizedFile);

	FJsonDocument Doc;
	if (!Doc.LoadFromFile(FilePath))
	{
		CATTY_CORE_ERROR("FResourceManager::LoadPackage: read failed '{}'", FilePath);
		LoadingFilePaths.erase(NormalizedFile);
		return {};
	}

	const FJsonValue& Root = Doc.GetRoot();
	if (!Root.IsObject())
	{
		CATTY_CORE_ERROR("FResourceManager::LoadPackage: root is not an object '{}'", FilePath);
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
				FGC* GC = Detail::GetGC();
				if (GC && GC->FindPackage(DepName))
				{
					continue;
				}
			}

			if (DepFile.empty())
			{
				CATTY_CORE_WARN(
					"FResourceManager::LoadPackage: dependency '{}' has empty file — skip",
					DepName.empty() ? "<unnamed>" : DepName);
				continue;
			}

			if (!LoadPackageInternal(DepFile, LoadingFilePaths))
			{
				CATTY_CORE_ERROR(
					"FResourceManager::LoadPackage: failed loading dependency '{}' from '{}'",
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

	FGC* GC = Detail::GetGC();
	if (FObjectRef Existing = GC ? GC->FindPackage(PackageName) : FObjectRef{})
	{
		CATTY_CORE_WARN(
			"FResourceManager::LoadPackage: '{}' already loaded — returning existing",
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
		CATTY_CORE_ERROR("FResourceManager::LoadPackage: NewObject<UPackage> failed");
		LoadingFilePaths.erase(NormalizedFile);
		return {};
	}

	if (!Raw->Deserialize(Root))
	{
		CATTY_CORE_ERROR("FResourceManager::LoadPackage: Deserialize failed '{}'", FilePath);
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
				CATTY_CORE_WARN(
					"FResourceManager::LoadPackage: unsupported class '{}' for '{}' — skip",
					ClassName,
					ObjectName);
				continue;
			}

			if (SourcePath.empty())
			{
				CATTY_CORE_WARN(
					"FResourceManager::LoadPackage: object '{}' has no source — skip",
					ObjectName);
				continue;
			}

			Created = LoadResourceIntoPackage(PackageRef, ObjectName, SourcePath, Type);

			if (!Created)
			{
				CATTY_CORE_WARN(
					"FResourceManager::LoadPackage: failed to create '{}' in '{}'",
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
				CATTY_CORE_WARN(
					"FResourceManager::LoadPackage: unresolved ref '{}' in '{}'",
					SoftPath,
					PackageName);
				Resolved.push_back(nullptr);
				continue;
			}
			Resolved.push_back(ResolvedRef.Get());
		}

		Links.Object->SetReferencedObjects(Resolved);
	}

	CATTY_CORE_INFO(
		"Loaded package '{}' ({} objects) from '{}'",
		Raw->GetName(),
		Raw->GetObjectCount(),
		FilePath);
	LoadingFilePaths.erase(NormalizedFile);
	return PackageRef;
}

EResourceLoadState FResourceManager::GetLoadState(const FObjectRef& Object) const
{
	const UResource* Resource = Object.Cast<UResource>();
	if (!Resource)
	{
		return EResourceLoadState::Invalid;
	}

	return Resource->GetLoadState();
}

bool FResourceManager::IsReady(const FObjectRef& Object) const
{
	return GetLoadState(Object) == EResourceLoadState::Ready;
}

void FResourceManager::Flush(const FObjectRef& Object)
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

void FResourceManager::FlushAll()
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

FResourceManager* GetResourceManager()
{
	if (!GApp)
	{
		return nullptr;
	}
	return GApp->GetExtension<FResourceManager>();
}

} // namespace Detail

} // namespace Catty
