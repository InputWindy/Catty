#include "Catty/Resource/ResourceManager.h"

#include "Catty/Core/ConsoleManager.h"
#include "Catty/Core/Json.h"
#include "Catty/Core/Log.h"
#include "Catty/Script/ScriptSystem.h"
#include "Resource/ResourceServer.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace Catty
{

namespace
{

static TAutoConsoleVariable GCVarPackagePoolInitial(
	"res.Pool.PackageInitial",
	16,
	"Initial FPackage pool slot count");

static TAutoConsoleVariable GCVarResourcePoolInitial(
	"res.Pool.ResourceInitial",
	64,
	"Initial FResource pool slot count");

[[nodiscard]] std::string GetExtensionLower(const std::string& Path)
{
	const std::size_t Slash = Path.find_last_of("/\\");
	const std::size_t Start = (Slash == std::string::npos) ? 0 : Slash + 1;
	const std::size_t Dot = Path.find_last_of('.');
	if (Dot == std::string::npos || Dot < Start)
	{
		return {};
	}

	std::string Ext = Path.substr(Dot + 1);
	for (char& Ch : Ext)
	{
		Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(Ch)));
	}
	return Ext;
}

} // namespace

FResourceManager::FResourceManager()
	: Server(std::make_unique<FResourceServer>())
{
}

FResourceManager::~FResourceManager()
{
	Shutdown();
}

void FResourceManager::BindLua(FScriptSystem& Script)
{
	(void)Script;
	// Catalog find/get is not ResourceManager's Lua surface.
	// Push FObjectRef / FPackage into Lua from game/engine call sites instead.
}

bool FResourceManager::IsInitialized() const
{
	return GC != nullptr && Server && Server->IsInitialized();
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

EResourceType FResourceManager::InferTypeFromPath(const std::string& Path)
{
	const std::string Ext = GetExtensionLower(Path);
	if (Ext == "png" || Ext == "jpg" || Ext == "jpeg" || Ext == "tga" || Ext == "bmp"
		|| Ext == "ktx" || Ext == "ktx2" || Ext == "dds")
	{
		return EResourceType::Texture;
	}
	if (Ext == "mesh" || Ext == "obj" || Ext == "fbx" || Ext == "gltf" || Ext == "glb")
	{
		return EResourceType::Mesh;
	}
	if (Ext == "mat" || Ext == "material")
	{
		return EResourceType::Material;
	}
	if (Ext == "spv" || Ext == "vert" || Ext == "frag" || Ext == "hlsl" || Ext == "glsl")
	{
		return EResourceType::Shader;
	}
	if (Ext == "wav" || Ext == "ogg" || Ext == "mp3" || Ext == "flac")
	{
		return EResourceType::Audio;
	}
	if (Ext == "json" || Ext == "bin" || Ext == "dat")
	{
		return EResourceType::Data;
	}
	if (!Ext.empty())
	{
		return EResourceType::Raw;
	}
	return EResourceType::Unknown;
}

EResourceType FResourceManager::ResourceTypeFromString(const std::string& Name)
{
	if (Name == "Raw") return EResourceType::Raw;
	if (Name == "Texture") return EResourceType::Texture;
	if (Name == "Mesh") return EResourceType::Mesh;
	if (Name == "Material") return EResourceType::Material;
	if (Name == "Shader") return EResourceType::Shader;
	if (Name == "Audio") return EResourceType::Audio;
	if (Name == "Data") return EResourceType::Data;
	return EResourceType::Unknown;
}

bool FResourceManager::Initialize(FGCManager& InGC)
{
	if (IsInitialized())
	{
		return true;
	}

	if (!InGC.IsInitialized())
	{
		CATTY_CORE_ERROR("FResourceManager::Initialize: FGCManager must be initialized first");
		return false;
	}

	if (!Server->Initialize())
	{
		CATTY_CORE_ERROR("FResourceManager::Initialize: FResourceServer failed");
		return false;
	}

	GC = &InGC;
	TransientPackage = nullptr;
	const int PackageSlots = (std::max)(1, GCVarPackagePoolInitial.GetValue());
	const int ResourceSlots = (std::max)(1, GCVarResourcePoolInitial.GetValue());
	PackagePool.Reserve(static_cast<std::size_t>(PackageSlots));
	ResourcePool.Reserve(static_cast<std::size_t>(ResourceSlots));
	DestroyHandlerId = GC->AddObjectDestroyHandler([this](FObject* Object) -> bool
	{
		return TryDestroyManagedObject(Object);
	});
	CATTY_CORE_INFO("ResourceManager initialized");
	return true;
}

bool FResourceManager::TryDestroyManagedObject(FObject* Object)
{
	if (FResource* Resource = dynamic_cast<FResource*>(Object))
	{
		DestroyResource(Resource);
		return true;
	}

	if (FPackage* Package = dynamic_cast<FPackage*>(Object))
	{
		DestroyPackage(Package);
		return true;
	}

	return false;
}

void FResourceManager::DestroyResource(FResource* Resource)
{
	if (!Resource)
	{
		return;
	}

	if (Server->IsInitialized() && Resource->GetId().IsValid())
	{
		Server->Release(Resource->GetId());
	}

	Resource->ClearOuter();

	if (GC)
	{
		GC->UnregisterObject(*Resource);
	}

	ResourcePool.Free(Resource);
}

void FResourceManager::DestroyPackage(FPackage* Package)
{
	if (!Package)
	{
		return;
	}

	DropPackageFromCatalog(Package);

	if (!Package->Objects.empty())
	{
		CATTY_CORE_ERROR(
			"FResourceManager::DestroyPackage: '{}' still has {} object(s) — destroying zero-ref leftovers",
			Package->GetName(),
			Package->GetObjectCount());
		(void)DestroyPackageObjects(*Package, true);
	}

	if (GC)
	{
		GC->UnregisterObject(*Package);
	}

	PackagePool.Free(Package);
}

void FResourceManager::DropPackageFromCatalog(FPackage* Package)
{
	if (!Package)
	{
		return;
	}

	const auto It = Packages.find(NormalizePackageName(Package->GetName()));
	if (It != Packages.end() && It->second.Get() == static_cast<FObject*>(Package))
	{
		Packages.erase(It); // releases catalog FObjectRef
	}

	if (TransientPackage == Package)
	{
		TransientPackage = nullptr;
	}
}

bool FResourceManager::DestroyPackageObjects(FPackage& Package, bool bForce)
{
	std::vector<std::string> Names;
	Names.reserve(Package.Objects.size());
	for (const auto& Pair : Package.Objects)
	{
		Names.push_back(Pair.first);
	}

	std::vector<FObject*> ToDestroy;
	ToDestroy.reserve(Names.size());

	for (const std::string& ObjectName : Names)
	{
		FObject* Object = nullptr;
		const auto It = Package.Objects.find(ObjectName);
		if (It != Package.Objects.end())
		{
			Object = It->second;
		}

		if (Object && Object->GetRefCount() > 0)
		{
			if (!bForce)
			{
				CATTY_CORE_ERROR(
					"FResourceManager::DestroyPackageObjects: '{}' still has live FObjectRef (count={})",
					Object->GetPathName(),
					Object->GetRefCount());
				return false;
			}

			CATTY_CORE_ERROR(
				"FResourceManager::DestroyPackageObjects: skipping '{}' (RefCount={})",
				Object->GetPathName(),
				Object->GetRefCount());
			continue;
		}

		if (Object)
		{
			Object->ClearOuter();
		}
		ToDestroy.push_back(Object);
	}

	for (FObject* Object : ToDestroy)
	{
		if (!Object)
		{
			continue;
		}

		if (FResource* Resource = dynamic_cast<FResource*>(Object))
		{
			DestroyResource(Resource);
		}
		else if (GC)
		{
			GC->DestroyObjectImmediate(Object);
		}
	}

	return Package.Objects.empty();
}

void FResourceManager::Shutdown()
{
	if (!GC && !Server->IsInitialized())
	{
		return;
	}

	std::vector<FPackage*> Snapshot;
	Snapshot.reserve(Packages.size());
	for (auto& Pair : Packages)
	{
		if (FPackage* Pkg = Pair.second.Cast<FPackage>())
		{
			Snapshot.push_back(Pkg);
		}
	}

	for (FPackage* Package : Snapshot)
	{
		if (!Package)
		{
			continue;
		}

		(void)DestroyPackageObjects(*Package, true);
		DropPackageFromCatalog(Package);
		if (Package->GetRefCount() == 0)
		{
			Package->MarkForImmediateDestroy();
		}
		else
		{
			CATTY_CORE_ERROR(
				"FResourceManager::Shutdown: package '{}' still RefCount={} — release Refs",
				Package->GetName(),
				Package->GetRefCount());
		}
	}

	if (GC)
	{
		GC->CollectGarbage();
		GC->PurgePendingKill();
	}

	if (GC && DestroyHandlerId != FGCManager::InvalidDestroyHandlerId)
	{
		GC->RemoveObjectDestroyHandler(DestroyHandlerId);
		DestroyHandlerId = FGCManager::InvalidDestroyHandlerId;
	}

	if (ResourcePool.GetNumLive() > 0)
	{
		CATTY_CORE_ERROR(
			"FResourceManager::Shutdown: {} resource slot(s) still live — not Clear()'ing pool",
			ResourcePool.GetNumLive());
	}
	else
	{
		ResourcePool.Clear();
	}

	if (PackagePool.GetNumLive() > 0)
	{
		CATTY_CORE_ERROR(
			"FResourceManager::Shutdown: {} package slot(s) still live — not Clear()'ing pool",
			PackagePool.GetNumLive());
	}
	else
	{
		PackagePool.Clear();
	}

	GC = nullptr;

	if (Server->IsInitialized())
	{
		Server->Shutdown();
	}

	CATTY_CORE_INFO("ResourceManager shut down");
}

FObjectRef FResourceManager::CreatePackage(std::string Name, EPackageFlags Flags)
{
	if (!IsInitialized())
	{
		CATTY_CORE_ERROR("FResourceManager::CreatePackage: not initialized");
		return {};
	}

	Name = NormalizePackageName(std::move(Name));
	if (Name.empty())
	{
		CATTY_CORE_ERROR("FResourceManager::CreatePackage: empty name");
		return {};
	}

	if (Packages.find(Name) != Packages.end())
	{
		CATTY_CORE_ERROR("FResourceManager::CreatePackage: package '{}' already exists", Name);
		return {};
	}

	FPackage* Package = PackagePool.Allocate(Name, Flags);
	GC->RegisterObject(*Package);
	FObjectRef CatalogRef = FObjectRef::Wrap(Package);
	if (Name == TransientPackageName)
	{
		TransientPackage = Package;
	}
	Packages.emplace(std::move(Name), std::move(CatalogRef));
	return FObjectRef::Wrap(Package);
}

FObjectRef FResourceManager::GetTransientPackage()
{
	if (TransientPackage)
	{
		return FObjectRef::Wrap(TransientPackage);
	}

	FObjectRef Existing = FindPackage(TransientPackageName);
	if (Existing)
	{
		TransientPackage = Existing.Cast<FPackage>();
		return Existing;
	}

	return CreatePackage(TransientPackageName, EPackageFlags::Transient);
}

FObjectRef FResourceManager::FindPackage(const std::string& Name) const
{
	const auto It = Packages.find(NormalizePackageName(Name));
	if (It == Packages.end() || !It->second)
	{
		return {};
	}
	return It->second;
}

bool FResourceManager::UnloadPackage(const std::string& Name)
{
	const std::string Key = NormalizePackageName(Name);
	if (Key == TransientPackageName)
	{
		CATTY_CORE_ERROR("FResourceManager::UnloadPackage: cannot unload transient package");
		return false;
	}

	const auto It = Packages.find(Key);
	if (It == Packages.end() || !It->second)
	{
		return false;
	}

	DropPackageFromCatalog(It->second.Cast<FPackage>());
	return true;
}

FObjectRef FResourceManager::CreateResource(
	const FObjectRef& Package,
	std::string ObjectName,
	std::string SourcePath,
	EResourceType Type)
{
	if (!IsInitialized())
	{
		CATTY_CORE_ERROR("FResourceManager::CreateResource: not initialized");
		return {};
	}

	if (!Package)
	{
		CATTY_CORE_ERROR("FResourceManager::CreateResource: invalid Package");
		return {};
	}

	FPackage* PackagePtr = Package.Cast<FPackage>();
	if (!PackagePtr)
	{
		CATTY_CORE_ERROR("FResourceManager::CreateResource: Ref is not an FPackage");
		return {};
	}

	FPackage& PackageObj = *PackagePtr;

	if (ObjectName.empty())
	{
		CATTY_CORE_ERROR("FResourceManager::CreateResource: empty ObjectName");
		return {};
	}

	if (SourcePath.empty())
	{
		CATTY_CORE_ERROR("FResourceManager::CreateResource: empty SourcePath");
		return {};
	}

	if (PackageObj.FindObject(ObjectName))
	{
		CATTY_CORE_ERROR(
			"FResourceManager::CreateResource: '{}' already exists in '{}'",
			ObjectName,
			PackageObj.GetName());
		return {};
	}

	if (Type == EResourceType::Unknown)
	{
		Type = InferTypeFromPath(SourcePath);
	}

	const FResourceId Id = Server->RequestLoad(SourcePath);
	if (!Id.IsValid())
	{
		return {};
	}

	FResource* Resource = ResourcePool.Allocate(
		&PackageObj,
		ObjectName,
		Id,
		Type,
		std::move(SourcePath));
	GC->RegisterObject(*Resource);

	if (!PackageObj.RegisterObject(Resource))
	{
		DestroyResource(Resource);
		return {};
	}

	return FObjectRef::Wrap(Resource);
}

FObjectRef FResourceManager::CreateResource(
	std::string SourcePath,
	EResourceType Type,
	std::string ObjectName)
{
	FObjectRef Package = GetTransientPackage();
	if (!Package)
	{
		return {};
	}

	if (ObjectName.empty())
	{
		ObjectName = MakeObjectNameFromSource(SourcePath);
	}

	// Avoid name clash in transient: append numeric suffix.
	std::string UniqueName = ObjectName;
	std::uint32_t Suffix = 1;
	FPackage* TransientPkg = Package.Cast<FPackage>();
	while (TransientPkg && TransientPkg->FindObject(UniqueName))
	{
		UniqueName = ObjectName + "_" + std::to_string(Suffix);
		++Suffix;
	}

	return CreateResource(Package, std::move(UniqueName), std::move(SourcePath), Type);
}

FObjectRef FResourceManager::FindObject(const FObjectRef& Package, const std::string& ObjectName) const
{
	FPackage* PackagePtr = Package.Cast<FPackage>();
	if (!PackagePtr)
	{
		return {};
	}
	return PackagePtr->FindObject(ObjectName);
}

FObjectRef FResourceManager::FindObject(const std::string& PackageName, const std::string& ObjectName) const
{
	FObjectRef Package = FindPackage(PackageName);
	FPackage* PackagePtr = Package.Cast<FPackage>();
	if (!PackagePtr)
	{
		return {};
	}
	return PackagePtr->FindObject(ObjectName);
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

	FPackage* PackagePtr = Package.Cast<FPackage>();
	if (!PackagePtr)
	{
		CATTY_CORE_ERROR("FResourceManager::SavePackage: Ref is not an FPackage");
		return false;
	}

	FPackage& PackageObj = *PackagePtr;

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

				FObjectRef DepPackage = FindPackage(DepName);
				FPackage* DepPtr = DepPackage.Cast<FPackage>();
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
	std::string PackageName;
	std::string ObjectName;
	if (!FObject::SplitObjectPath(PathName, PackageName, ObjectName))
	{
		return {};
	}

	return FindObject(PackageName, ObjectName);
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
				if (FindPackage(DepName))
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

	if (PackageName == TransientPackageName)
	{
		CATTY_CORE_ERROR("FResourceManager::LoadPackage: cannot load into transient package name");
		LoadingFilePaths.erase(NormalizedFile);
		return {};
	}

	if (const auto Existing = Packages.find(PackageName); Existing != Packages.end())
	{
		DropPackageFromCatalog(Existing->second.Cast<FPackage>());
	}

	FPackage* Raw = PackagePool.Allocate(PackageName, EPackageFlags::Persistent);
	if (!Raw->Deserialize(Root))
	{
		CATTY_CORE_ERROR("FResourceManager::LoadPackage: Deserialize failed '{}'", FilePath);
		PackagePool.Free(Raw);
		LoadingFilePaths.erase(NormalizedFile);
		return {};
	}

	Raw->AddPackageFlags(EPackageFlags::Persistent);
	Raw->ClearPackageFlags(EPackageFlags::Transient);
	Raw->SetFilePath(FilePath);
	GC->RegisterObject(*Raw);
	Packages.emplace(PackageName, FObjectRef::Wrap(Raw));
	FObjectRef PackageRef = FObjectRef::Wrap(Raw);

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
			if (ClassName == "Resource" || ClassName == "Object" || ClassName.empty())
			{
				if (SourcePath.empty())
				{
					CATTY_CORE_WARN(
						"FResourceManager::LoadPackage: object '{}' has no source — skip",
						ObjectName);
					continue;
				}

				EResourceType Type = EResourceType::Unknown;
				if (Entry.HasField("type"))
				{
					Type = ResourceTypeFromString(Entry.GetField("type").AsString());
				}

				if (ClassName != "Resource" && ClassName != "Object" && !ClassName.empty())
				{
					CATTY_CORE_WARN(
						"FResourceManager::LoadPackage: unknown class '{}' for '{}' — treating as Resource",
						ClassName,
						ObjectName);
				}

				Created = CreateResource(PackageRef, ObjectName, SourcePath, Type);
			}
			else
			{
				CATTY_CORE_WARN(
					"FResourceManager::LoadPackage: unsupported class '{}' for '{}' — skip",
					ClassName,
					ObjectName);
				continue;
			}

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

		std::vector<FObject*> Resolved;
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
	if (!IsInitialized())
	{
		return EResourceLoadState::Invalid;
	}

	const FResource* Resource = Object.Cast<FResource>();
	if (!Resource)
	{
		return EResourceLoadState::Invalid;
	}

	return Server->GetLoadState(Resource->GetId());
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

	if (const FResource* Resource = Object.Cast<FResource>())
	{
		Server->Flush(Resource->GetId());
	}
}

void FResourceManager::FlushAll()
{
	if (IsInitialized())
	{
		Server->FThreadedServer::Flush();
	}
}

} // namespace Catty
