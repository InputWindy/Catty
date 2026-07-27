#include "Catty/Resource/ResourceManager.h"

#include "Catty/Core/Json.h"
#include "Catty/Core/Log.h"
#include "Catty/Resource/ResourceServer.h"

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

bool FResourceManager::Initialize(FResourceServer& InServer)
{
	if (Server)
	{
		return true;
	}

	Server = &InServer;
	GcAccumulatorSeconds = 0.0f;
	TransientPackage = nullptr;
	CATTY_CORE_INFO("ResourceManager initialized");
	return true;
}

void FResourceManager::DestroyResourceExport(FResource& Resource)
{
	if (Server && Resource.GetId().IsValid())
	{
		Server->Release(Resource.GetId());
	}
}

void FResourceManager::DestroyPackageExports(FPackage& Package)
{
	for (FObject* Object : Package.GetExports())
	{
		if (FResource* Resource = dynamic_cast<FResource*>(Object))
		{
			DestroyResourceExport(*Resource);
		}
	}
	Package.ClearExports();
}

void FResourceManager::Shutdown()
{
	if (!Server)
	{
		return;
	}

	for (auto& Pair : Packages)
	{
		if (Pair.second)
		{
			DestroyPackageExports(*Pair.second);
		}
	}
	Packages.clear();
	TransientPackage = nullptr;
	Server = nullptr;
	CATTY_CORE_INFO("ResourceManager shut down");
}

FPackage* FResourceManager::CreatePackage(std::string Name, EPackageFlags Flags)
{
	if (!Server)
	{
		CATTY_CORE_ERROR("FResourceManager::CreatePackage: not initialized");
		return nullptr;
	}

	Name = NormalizePackageName(std::move(Name));
	if (Name.empty())
	{
		CATTY_CORE_ERROR("FResourceManager::CreatePackage: empty name");
		return nullptr;
	}

	if (Packages.find(Name) != Packages.end())
	{
		CATTY_CORE_ERROR("FResourceManager::CreatePackage: package '{}' already exists", Name);
		return nullptr;
	}

	auto Package = std::make_unique<FPackage>(Name, Flags);
	FPackage* Raw = Package.get();
	if (Name == TransientPackageName)
	{
		TransientPackage = Raw;
	}
	Packages.emplace(std::move(Name), std::move(Package));
	return Raw;
}

FPackage* FResourceManager::GetTransientPackage()
{
	if (TransientPackage)
	{
		return TransientPackage;
	}

	if (FPackage* Existing = FindPackage(TransientPackageName))
	{
		TransientPackage = Existing;
		return TransientPackage;
	}

	return CreatePackage(TransientPackageName, EPackageFlags::Transient);
}

FPackage* FResourceManager::FindPackage(const std::string& Name) const
{
	const auto It = Packages.find(NormalizePackageName(Name));
	if (It == Packages.end())
	{
		return nullptr;
	}
	return It->second.get();
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

	DestroyPackageExports(*It->second);
	Packages.erase(It);
	return true;
}

FResourceRef FResourceManager::CreateResource(
	FPackage& Package,
	std::string ObjectName,
	std::string SourcePath,
	EResourceType Type)
{
	if (!Server)
	{
		CATTY_CORE_ERROR("FResourceManager::CreateResource: not initialized");
		return {};
	}

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

	if (Package.FindObject(ObjectName))
	{
		CATTY_CORE_ERROR(
			"FResourceManager::CreateResource: '{}' already exists in '{}'",
			ObjectName,
			Package.GetName());
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

	auto Resource = std::make_unique<FResource>(
		&Package,
		ObjectName,
		Id,
		Type,
		std::move(SourcePath));
	FResource* Raw = Resource.get();

	if (!Package.RegisterExport(std::move(Resource)))
	{
		Server->Release(Id);
		return {};
	}

	return FResourceRef(Raw);
}

FResourceRef FResourceManager::CreateResource(
	std::string SourcePath,
	EResourceType Type,
	std::string ObjectName)
{
	FPackage* Package = GetTransientPackage();
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

	return CreateResource(*Package, std::move(UniqueName), std::move(SourcePath), Type);
}

FObject* FResourceManager::FindObject(FPackage& Package, const std::string& ObjectName) const
{
	return Package.FindObject(ObjectName);
}

FObject* FResourceManager::FindObject(const std::string& PackageName, const std::string& ObjectName) const
{
	FPackage* Package = FindPackage(PackageName);
	if (!Package)
	{
		return nullptr;
	}
	return Package->FindObject(ObjectName);
}

bool FResourceManager::SavePackage(FPackage& Package, const std::string& FilePath, bool bPretty)
{
	if (!Server)
	{
		CATTY_CORE_ERROR("FResourceManager::SavePackage: not initialized");
		return false;
	}

	const std::string OutPath = FilePath.empty() ? Package.GetFilePath() : FilePath;
	if (OutPath.empty())
	{
		CATTY_CORE_ERROR("FResourceManager::SavePackage: empty file path");
		return false;
	}

	if (!Package.IsPersistent())
	{
		CATTY_CORE_ERROR(
			"FResourceManager::SavePackage: package '{}' is Transient — mark Persistent first",
			Package.GetName());
		return false;
	}

	FJsonValue Root = FJsonValue::Object();
	if (!Package.Serialize(Root))
	{
		CATTY_CORE_ERROR("FResourceManager::SavePackage: Serialize failed for '{}'", Package.GetName());
		return false;
	}

	FJsonDocument Doc;
	Doc.SetRoot(std::move(Root));
	if (!Doc.SaveToFile(OutPath, bPretty))
	{
		CATTY_CORE_ERROR("FResourceManager::SavePackage: write failed '{}'", OutPath);
		return false;
	}

	Package.SetFilePath(OutPath);
	CATTY_CORE_INFO(
		"Saved package '{}' ({} exports) -> '{}'",
		Package.GetName(),
		Package.GetExportCount(),
		OutPath);
	return true;
}

FPackage* FResourceManager::LoadPackage(const std::string& FilePath)
{
	if (!Server)
	{
		CATTY_CORE_ERROR("FResourceManager::LoadPackage: not initialized");
		return nullptr;
	}

	if (FilePath.empty())
	{
		CATTY_CORE_ERROR("FResourceManager::LoadPackage: empty file path");
		return nullptr;
	}

	FJsonDocument Doc;
	if (!Doc.LoadFromFile(FilePath))
	{
		CATTY_CORE_ERROR("FResourceManager::LoadPackage: read failed '{}'", FilePath);
		return nullptr;
	}

	const FJsonValue& Root = Doc.GetRoot();
	if (!Root.IsObject())
	{
		CATTY_CORE_ERROR("FResourceManager::LoadPackage: root is not an object '{}'", FilePath);
		return nullptr;
	}

	std::string PackageName = NormalizePackageName(Root.GetField("name").AsString());
	if (PackageName.empty())
	{
		PackageName = FilePath;
	}

	if (PackageName == TransientPackageName)
	{
		CATTY_CORE_ERROR("FResourceManager::LoadPackage: cannot load into transient package name");
		return nullptr;
	}

	if (const auto Existing = Packages.find(PackageName); Existing != Packages.end())
	{
		DestroyPackageExports(*Existing->second);
		Packages.erase(Existing);
	}

	auto Package = std::make_unique<FPackage>(PackageName, EPackageFlags::Persistent);
	if (!Package->Deserialize(Root))
	{
		CATTY_CORE_ERROR("FResourceManager::LoadPackage: Deserialize failed '{}'", FilePath);
		return nullptr;
	}

	Package->AddFlags(EPackageFlags::Persistent);
	Package->ClearFlags(EPackageFlags::Transient);
	Package->SetFilePath(FilePath);

	FPackage* Raw = Package.get();
	Packages.emplace(PackageName, std::move(Package));

	const FJsonValue ExportField = Root.HasField("exports")
		? Root.GetField("exports")
		: (Root.HasField("resources") ? Root.GetField("resources") : FJsonValue::Null());

	if (ExportField.IsArray())
	{
		const std::size_t Count = ExportField.GetArraySize();
		for (std::size_t Index = 0; Index < Count; ++Index)
		{
			const FJsonValue Entry = ExportField.GetElement(Index);
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

			FResourceRef Ref = CreateResource(*Raw, ObjectName, SourcePath, Type);
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
		"Loaded package '{}' ({} exports) from '{}'",
		Raw->GetName(),
		Raw->GetExportCount(),
		FilePath);
	return Raw;
}

void FResourceManager::CollectGarbage()
{
	if (!Server)
	{
		return;
	}

	for (auto& Pair : Packages)
	{
		FPackage* Package = Pair.second.get();
		if (!Package || !Package->IsTransient())
		{
			continue;
		}

		std::vector<std::string> PendingRemove;
		for (FObject* Object : Package->GetExports())
		{
			if (Object && Object->GetRefCount() == 0)
			{
				PendingRemove.push_back(Object->GetName());
			}
		}

		for (const std::string& ObjectName : PendingRemove)
		{
			std::unique_ptr<FObject> Removed = Package->UnregisterExport(ObjectName);
			if (!Removed)
			{
				continue;
			}

			if (FResource* Resource = dynamic_cast<FResource*>(Removed.get()))
			{
				CATTY_CORE_INFO(
					"Resource GC: package='{}' object='{}' source=\"{}\"",
					Package->GetName(),
					Resource->GetName(),
					Resource->GetSourcePath());
				DestroyResourceExport(*Resource);
			}
		}
	}
}

void FResourceManager::TickGarbageCollection(float DeltaSeconds)
{
	if (GcIntervalSeconds <= 0.0f)
	{
		CollectGarbage();
		return;
	}

	GcAccumulatorSeconds += DeltaSeconds;
	if (GcAccumulatorSeconds >= GcIntervalSeconds)
	{
		GcAccumulatorSeconds = 0.0f;
		CollectGarbage();
	}
}

EResourceLoadState FResourceManager::GetLoadState(FResourceId Id) const
{
	return Server ? Server->GetLoadState(Id) : EResourceLoadState::Invalid;
}

bool FResourceManager::IsReady(FResourceId Id) const
{
	return Server && Server->IsReady(Id);
}

void FResourceManager::Flush(const FResource& Resource)
{
	if (Server)
	{
		Server->Flush(Resource.GetId());
	}
}

void FResourceManager::Flush(const FResourceRef& Resource)
{
	if (Resource.IsValid())
	{
		Flush(*Resource);
	}
}

void FResourceManager::FlushAll()
{
	if (Server)
	{
		Server->FThreadedServer::Flush();
	}
}

} // namespace Catty
