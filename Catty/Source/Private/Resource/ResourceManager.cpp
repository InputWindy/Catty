#include "Catty/Resource/ResourceManager.h"

#include "Catty/Core/Json.h"
#include "Catty/Core/Log.h"

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

FResourceManager::~FResourceManager()
{
	Shutdown();
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

	if (!Server.Initialize())
	{
		CATTY_CORE_ERROR("FResourceManager::Initialize: FResourceServer failed");
		return false;
	}

	GC = &InGC;
	TransientPackage = nullptr;
	PackagePool.Reserve(16);
	ResourcePool.Reserve(64);
	GC->AddObjectDestroyHandler([this](FObject* Object) -> bool
	{
		return TryDestroyResourceObject(Object);
	});
	CATTY_CORE_INFO("ResourceManager initialized");
	return true;
}

bool FResourceManager::TryDestroyResourceObject(FObject* Object)
{
	FResource* Resource = dynamic_cast<FResource*>(Object);
	if (!Resource)
	{
		return false;
	}

	DestroyResource(Resource);
	return true;
}

void FResourceManager::DestroyResource(FResource* Resource)
{
	if (!Resource)
	{
		return;
	}

	if (Server.IsInitialized() && Resource->GetId().IsValid())
	{
		Server.Release(Resource->GetId());
	}

	if (FPackage* Package = Resource->Outer)
	{
		const auto It = Package->Objects.find(Resource->GetName());
		if (It != Package->Objects.end() && It->second == Resource)
		{
			(void)Package->UnregisterObject(Resource->GetName());
		}
	}

	if (GC)
	{
		GC->UnregisterObject(*Resource);
	}

	ResourcePool.Free(Resource);
}

void FResourceManager::DestroyPackageObjects(FPackage& Package)
{
	// Snapshot raw pointers without AddRef — Free must not race with live FObjectRef.
	std::vector<FObject*> ObjectList;
	ObjectList.reserve(Package.Objects.size());
	for (const auto& Pair : Package.Objects)
	{
		ObjectList.push_back(Pair.second);
	}
	Package.ClearObjects();

	for (FObject* Object : ObjectList)
	{
		if (FResource* Resource = dynamic_cast<FResource*>(Object))
		{
			DestroyResource(Resource);
		}
		else if (Object && GC)
		{
			GC->DestroyObjectImmediate(Object);
		}
	}
}

void FResourceManager::UnregisterAndReleasePackage(FPackage* Package)
{
	if (!Package)
	{
		return;
	}

	DestroyPackageObjects(*Package);

	if (TransientPackage == Package)
	{
		TransientPackage = nullptr;
	}

	// Drop catalog Ref — Frees when no FPackageRef remains.
	Package->ReleaseRef();
}

void FResourceManager::FreePackageMemory(FPackage* Package)
{
	if (!Package)
	{
		return;
	}

	Package->Owner = nullptr;
	PackagePool.Free(Package);
}

void FResourceManager::Shutdown()
{
	if (!GC && !Server.IsInitialized())
	{
		return;
	}

	for (auto& Pair : Packages)
	{
		if (FPackage* Package = Pair.second)
		{
			DestroyPackageObjects(*Package);
			Package->Owner = nullptr;
		}
	}
	Packages.clear();
	TransientPackage = nullptr;

	if (GC)
	{
		GC->ClearObjectDestroyHandlers();
	}

	ResourcePool.Clear();
	PackagePool.Clear();
	GC = nullptr;

	if (Server.IsInitialized())
	{
		Server.Shutdown();
	}

	CATTY_CORE_INFO("ResourceManager shut down");
}

FPackageRef FResourceManager::CreatePackage(std::string Name, EPackageFlags Flags)
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
	Package->SetOwner(this);
	Package->AddRef(); // catalog Ref while registered in Packages
	if (Name == TransientPackageName)
	{
		TransientPackage = Package;
	}
	Packages.emplace(std::move(Name), Package);
	return FPackageRef(Package);
}

FPackageRef FResourceManager::GetTransientPackage()
{
	if (TransientPackage)
	{
		return FPackageRef(TransientPackage);
	}

	FPackageRef Existing = FindPackage(TransientPackageName);
	if (Existing)
	{
		TransientPackage = Existing.Get();
		return Existing;
	}

	return CreatePackage(TransientPackageName, EPackageFlags::Transient);
}

FPackageRef FResourceManager::FindPackage(const std::string& Name) const
{
	const auto It = Packages.find(NormalizePackageName(Name));
	if (It == Packages.end() || !It->second)
	{
		return {};
	}
	return FPackageRef(It->second);
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

	FPackage* Package = It->second;
	Packages.erase(It);
	UnregisterAndReleasePackage(Package);
	return true;
}

FObjectRef FResourceManager::CreateResource(
	const FPackageRef& Package,
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

	FPackage& PackageRef = *Package;

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

	if (PackageRef.FindObject(ObjectName))
	{
		CATTY_CORE_ERROR(
			"FResourceManager::CreateResource: '{}' already exists in '{}'",
			ObjectName,
			PackageRef.GetName());
		return {};
	}

	if (Type == EResourceType::Unknown)
	{
		Type = InferTypeFromPath(SourcePath);
	}

	const FResourceId Id = Server.RequestLoad(SourcePath);
	if (!Id.IsValid())
	{
		return {};
	}

	FResource* Resource = ResourcePool.Allocate(
		&PackageRef,
		ObjectName,
		Id,
		Type,
		std::move(SourcePath));
	GC->RegisterObject(*Resource);

	if (!PackageRef.RegisterObject(Resource))
	{
		DestroyResource(Resource);
		return {};
	}

	return FObjectRef(Resource);
}

FObjectRef FResourceManager::CreateResource(
	std::string SourcePath,
	EResourceType Type,
	std::string ObjectName)
{
	FPackageRef Package = GetTransientPackage();
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
	while (Package->FindObject(UniqueName))
	{
		UniqueName = ObjectName + "_" + std::to_string(Suffix);
		++Suffix;
	}

	return CreateResource(Package, std::move(UniqueName), std::move(SourcePath), Type);
}

FObjectRef FResourceManager::FindObject(const FPackageRef& Package, const std::string& ObjectName) const
{
	if (!Package)
	{
		return {};
	}
	return Package->FindObject(ObjectName);
}

FObjectRef FResourceManager::FindObject(const std::string& PackageName, const std::string& ObjectName) const
{
	FPackageRef Package = FindPackage(PackageName);
	if (!Package)
	{
		return {};
	}
	return Package->FindObject(ObjectName);
}

bool FResourceManager::SavePackage(const FPackageRef& Package, const std::string& FilePath, bool bPretty)
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

	const std::string OutPath = FilePath.empty() ? Package->GetFilePath() : FilePath;
	if (OutPath.empty())
	{
		CATTY_CORE_ERROR("FResourceManager::SavePackage: empty file path");
		return false;
	}

	if (!Package->IsPersistent())
	{
		CATTY_CORE_ERROR(
			"FResourceManager::SavePackage: package '{}' is Transient — mark Persistent first",
			Package->GetName());
		return false;
	}

	FJsonValue Root = FJsonValue::Object();
	if (!Package->Serialize(Root))
	{
		CATTY_CORE_ERROR("FResourceManager::SavePackage: Serialize failed for '{}'", Package->GetName());
		return false;
	}

	FJsonDocument Doc;
	Doc.SetRoot(std::move(Root));
	if (!Doc.SaveToFile(OutPath, bPretty))
	{
		CATTY_CORE_ERROR("FResourceManager::SavePackage: write failed '{}'", OutPath);
		return false;
	}

	Package->SetFilePath(OutPath);
	CATTY_CORE_INFO(
		"Saved package '{}' ({} objects) -> '{}'",
		Package->GetName(),
		Package->GetObjectCount(),
		OutPath);
	return true;
}

FPackageRef FResourceManager::LoadPackage(const std::string& FilePath)
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

	FJsonDocument Doc;
	if (!Doc.LoadFromFile(FilePath))
	{
		CATTY_CORE_ERROR("FResourceManager::LoadPackage: read failed '{}'", FilePath);
		return {};
	}

	const FJsonValue& Root = Doc.GetRoot();
	if (!Root.IsObject())
	{
		CATTY_CORE_ERROR("FResourceManager::LoadPackage: root is not an object '{}'", FilePath);
		return {};
	}

	std::string PackageName = NormalizePackageName(Root.GetField("name").AsString());
	if (PackageName.empty())
	{
		PackageName = FilePath;
	}

	if (PackageName == TransientPackageName)
	{
		CATTY_CORE_ERROR("FResourceManager::LoadPackage: cannot load into transient package name");
		return {};
	}

	if (const auto Existing = Packages.find(PackageName); Existing != Packages.end())
	{
		FPackage* Old = Existing->second;
		Packages.erase(Existing);
		UnregisterAndReleasePackage(Old);
	}

	FPackage* Raw = PackagePool.Allocate(PackageName, EPackageFlags::Persistent);
	Raw->SetOwner(this);
	if (!Raw->Deserialize(Root))
	{
		CATTY_CORE_ERROR("FResourceManager::LoadPackage: Deserialize failed '{}'", FilePath);
		PackagePool.Free(Raw);
		return {};
	}

	Raw->AddFlags(EPackageFlags::Persistent);
	Raw->ClearFlags(EPackageFlags::Transient);
	Raw->SetFilePath(FilePath);
	Raw->AddRef(); // catalog
	Packages.emplace(PackageName, Raw);

	FJsonValue ObjectField = FJsonValue::Null();
	if (Root.HasField("objects"))
	{
		ObjectField = Root.GetField("objects");
	}
	else if (Root.HasField("exports"))
	{
		// Legacy package JSON key.
		ObjectField = Root.GetField("exports");
	}
	else if (Root.HasField("resources"))
	{
		ObjectField = Root.GetField("resources");
	}

	FPackageRef PackageRef(Raw);

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

			if (SourcePath.empty())
			{
				continue;
			}

			if (ObjectName.empty())
			{
				ObjectName = MakeObjectNameFromSource(SourcePath);
			}

			EResourceType Type = EResourceType::Unknown;
			if (Entry.HasField("type"))
			{
				Type = ResourceTypeFromString(Entry.GetField("type").AsString());
			}

			const std::string ClassName = Entry.GetField("class").AsString("Resource");
			if (ClassName != "Resource" && ClassName != "Object" && !ClassName.empty())
			{
				CATTY_CORE_WARN(
					"FResourceManager::LoadPackage: unknown class '{}' for '{}' — treating as Resource",
					ClassName,
					ObjectName);
			}

			FObjectRef Ref = CreateResource(PackageRef, ObjectName, SourcePath, Type);
			if (!Ref.IsValid())
			{
				CATTY_CORE_WARN(
					"FResourceManager::LoadPackage: failed to create '{}' in '{}'",
					ObjectName,
					PackageName);
			}
		}
	}

	CATTY_CORE_INFO(
		"Loaded package '{}' ({} objects) from '{}'",
		Raw->GetName(),
		Raw->GetObjectCount(),
		FilePath);
	return PackageRef;
}

void FResourceManager::CollectGarbage()
{
	if (GC)
	{
		GC->CollectGarbage();
	}
}

void FResourceManager::TickGarbageCollection(float DeltaSeconds)
{
	if (GC)
	{
		GC->Tick(DeltaSeconds);
	}
}

EResourceLoadState FResourceManager::GetLoadState(FResourceId Id) const
{
	return IsInitialized() ? Server.GetLoadState(Id) : EResourceLoadState::Invalid;
}

bool FResourceManager::IsReady(FResourceId Id) const
{
	return IsInitialized() && Server.IsReady(Id);
}

void FResourceManager::Flush(const FObjectRef& Object)
{
	if (!IsInitialized())
	{
		return;
	}

	if (const FResource* Resource = Object.Cast<FResource>())
	{
		Server.Flush(Resource->GetId());
	}
}

void FResourceManager::FlushAll()
{
	if (IsInitialized())
	{
		Server.FThreadedServer::Flush();
	}
}

} // namespace Catty
