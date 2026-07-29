#include <Core/Resource/ResourceManager.h>

#include <Core/App.h>
#include <Core/GC.h>
#include <Core/Json.h>
#include <Core/Log.h>
#include <Core/Modules/ResourceModule.h>
#include <Core/Paths.h>
#include <Core/SoftObjectPath.h>
#include "Core/Resource/ResourceServer.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace Catty
{

namespace
{

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

	CATTY_CORE_INFO("ResourceManager initialized");
	return true;
}

bool FResourceManager::RegisterResource(const FObjectRef& Resource)
{
	FResource* ResourcePtr = Resource.Cast<FResource>();
	if (!ResourcePtr)
	{
		CATTY_CORE_ERROR("FResourceManager::RegisterResource: Ref is not an FResource");
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

bool FResourceManager::UnregisterResource(FObject* Resource)
{
	FResource* ResourcePtr = dynamic_cast<FResource*>(Resource);
	if (!ResourcePtr)
	{
		return false;
	}

	const std::string Key = MakeResourceCatalogKey(*ResourcePtr);
	if (Key.empty())
	{
		return false;
	}

	const auto It = Resources.find(Key);
	if (It == Resources.end() || It->second.Get() != static_cast<FObject*>(ResourcePtr))
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

void FResourceManager::ReleaseResourceId(FResourceId Id)
{
	if (Server && Server->IsInitialized() && Id.IsValid())
	{
		Server->Release(Id);
	}
}

std::string FResourceManager::MakeResourceCatalogKey(const FResource& Resource)
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
	std::vector<FObject*> Snapshot;
	for (const auto& Pair : Resources)
	{
		FResource* Resource = Pair.second.Cast<FResource>();
		if (!Resource)
		{
			continue;
		}

		FObjectRef PackageRef = Resource->GetPackage();
		FPackage* Package = PackageRef.Cast<FPackage>();
		if (Package && NormalizePackageName(Package->GetName()) == Key)
		{
			Snapshot.push_back(Resource);
		}
	}

	for (FObject* Resource : Snapshot)
	{
		UnregisterResource(Resource);
	}
}

void FResourceManager::Shutdown()
{
	if (!IsInitialized())
	{
		return;
	}

	std::vector<FObjectRef> Snapshot;
	Snapshot.reserve(Resources.size());
	for (auto& Pair : Resources)
	{
		Snapshot.push_back(Pair.second);
	}

	for (FObjectRef& Ref : Snapshot)
	{
		if (FResource* Resource = Ref.Cast<FResource>())
		{
			UnregisterResource(Resource);
			Resource->ClearOuter();
		}
	}

	Resources.clear();
	Snapshot.clear();

	if (FGC* GC = Detail::GetGC())
	{
		GC->CollectGarbage();
		GC->PurgePendingKill();

		const std::size_t LiveResources = GC->GetPooledLiveCount<FResource>();
		const std::size_t LivePackages = GC->GetPooledLiveCount<FPackage>();
		if (LiveResources > 0)
		{
			CATTY_CORE_ERROR(
				"FResourceManager::Shutdown: {} resource slot(s) still live in GC pool",
				LiveResources);
		}
		if (LivePackages > 0)
		{
			CATTY_CORE_ERROR(
				"FResourceManager::Shutdown: {} package slot(s) still live in GC pool",
				LivePackages);
		}
	}

	if (Server->IsInitialized())
	{
		Server->Shutdown();
	}

	CATTY_CORE_INFO("ResourceManager shut down");
}

FObjectRef FResourceManager::LoadResourceIntoPackage(
	const FObjectRef& Package,
	std::string ObjectName,
	std::string SourcePath,
	EResourceType Type)
{
	if (!IsInitialized())
	{
		CATTY_CORE_ERROR("FResourceManager::LoadResourceIntoPackage: not initialized");
		return {};
	}

	if (!Package)
	{
		CATTY_CORE_ERROR("FResourceManager::LoadResourceIntoPackage: invalid Package");
		return {};
	}

	FPackage* PackagePtr = Package.Cast<FPackage>();
	if (!PackagePtr)
	{
		CATTY_CORE_ERROR("FResourceManager::LoadResourceIntoPackage: Ref is not an FPackage");
		return {};
	}

	FPackage& PackageObj = *PackagePtr;

	if (ObjectName.empty())
	{
		CATTY_CORE_ERROR("FResourceManager::LoadResourceIntoPackage: empty ObjectName");
		return {};
	}

	if (SourcePath.empty())
	{
		CATTY_CORE_ERROR("FResourceManager::LoadResourceIntoPackage: empty SourcePath");
		return {};
	}

	if (PackageObj.FindObject(ObjectName))
	{
		CATTY_CORE_ERROR(
			"FResourceManager::LoadResourceIntoPackage: '{}' already exists in '{}'",
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

	FGC* GC = Detail::GetGC();
	if (!GC)
	{
		CATTY_CORE_ERROR("FResourceManager::LoadResourceIntoPackage: GC unavailable");
		if (Server->IsInitialized() && Id.IsValid())
		{
			Server->Release(Id);
		}
		return {};
	}

	FObjectRef ResourceRef = GC->NewObject<FResource>(
		&PackageObj,
		ObjectName,
		Id,
		Type,
		std::move(SourcePath));
	FResource* Resource = ResourceRef.Cast<FResource>();
	if (!Resource)
	{
		CATTY_CORE_ERROR("FResourceManager::LoadResourceIntoPackage: NewObject<FResource> failed");
		if (Server->IsInitialized() && Id.IsValid())
		{
			Server->Release(Id);
		}
		return {};
	}

	if (!PackageObj.RegisterObject(Resource))
	{
		Resource->ClearOuter();
		return {};
	}

	if (!RegisterResource(ResourceRef))
	{
		Resource->ClearOuter();
		return {};
	}

	return ResourceRef;
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
	return FindResourceByPath(NormalizePackageName(PackageName) + "." + ObjectName);
}

FObjectRef FResourceManager::FindResourceByPath(const std::string& VirtualPath) const
{
	const std::string Key = NormalizeResourceVirtualPath(VirtualPath);
	if (Key.empty())
	{
		return {};
	}

	const auto It = Resources.find(Key);
	if (It == Resources.end() || !It->second)
	{
		return {};
	}
	return It->second;
}

bool FResourceManager::UnloadResource(const std::string& VirtualPath)
{
	const std::string Key = NormalizeResourceVirtualPath(VirtualPath);
	if (Key.empty())
	{
		return false;
	}

	const auto It = Resources.find(Key);
	if (It == Resources.end() || !It->second)
	{
		return false;
	}

	return UnregisterResource(It->second.Cast<FResource>());
}

bool FResourceManager::UnloadResource(const FObjectRef& Resource)
{
	FResource* ResourcePtr = Resource.Cast<FResource>();
	if (!ResourcePtr)
	{
		return false;
	}

	return UnregisterResource(ResourcePtr);
}

FObjectRef FResourceManager::Resolve(const FSoftObjectPath& SoftPath) const
{
	if (!SoftPath.IsValid())
	{
		return {};
	}

	if (SoftPath.HasSubPath())
	{
		CATTY_CORE_WARN(
			"FResourceManager::Resolve: subobject path not implemented yet ('{}') — resolving asset only",
			SoftPath.ToStringWithoutClass());
	}

	return FindObject(SoftPath.GetPackageName(), SoftPath.GetAssetName());
}

FObjectRef FResourceManager::Resolve(const std::string& SoftPathString) const
{
	FSoftObjectPath SoftPath;
	if (!SoftPath.TrySetPath(SoftPathString) || !SoftPath.IsValid())
	{
		return {};
	}
	return Resolve(SoftPath);
}

FObjectRef FResourceManager::TryLoad(const FSoftObjectPath& SoftPath)
{
	if (!SoftPath.IsValid())
	{
		return {};
	}

	if (FObjectRef Existing = Resolve(SoftPath))
	{
		return Existing;
	}

	FGC* GC = Detail::GetGC();
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

	return Resolve(SoftPath);
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

				FGC* GC = Detail::GetGC();
				FObjectRef DepPackage = GC ? GC->FindPackage(DepName) : FObjectRef{};
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
	return Resolve(PathName);
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
		? GC->NewObject<FPackage>(PackageName, EPackageFlags::Persistent)
		: FObjectRef{};
	FPackage* Raw = PackageRef.Cast<FPackage>();
	if (!Raw)
	{
		CATTY_CORE_ERROR("FResourceManager::LoadPackage: NewObject<FPackage> failed");
		LoadingFilePaths.erase(NormalizedFile);
		return {};
	}

	if (!Raw->Deserialize(Root))
	{
		CATTY_CORE_ERROR("FResourceManager::LoadPackage: Deserialize failed '{}'", FilePath);
		LoadingFilePaths.erase(NormalizedFile);
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

				Created = LoadResourceIntoPackage(PackageRef, ObjectName, SourcePath, Type);
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

namespace Detail
{

FResourceManager* GetResourceManager()
{
	if (!GApp)
	{
		return nullptr;
	}
	FResourceModule* Module = GApp->GetModule<FResourceModule>();
	return Module ? &Module->GetResourceManager() : nullptr;
}

} // namespace Detail

} // namespace Catty
