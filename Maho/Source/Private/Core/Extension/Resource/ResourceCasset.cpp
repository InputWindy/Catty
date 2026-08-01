#include "ResourceCasset.h"

#include <Core/System/Log.h>
#include <Core/Object/SoftObjectPath.h>

#include <cstring>
#include <vector>

namespace Maho
{
namespace ResourceCasset
{
namespace
{

constexpr char kBase64Table[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

[[nodiscard]] int Base64DecodeValue(char C)
{
	if (C >= 'A' && C <= 'Z')
	{
		return C - 'A';
	}
	if (C >= 'a' && C <= 'z')
	{
		return C - 'a' + 26;
	}
	if (C >= '0' && C <= '9')
	{
		return C - '0' + 52;
	}
	if (C == '+')
	{
		return 62;
	}
	if (C == '/')
	{
		return 63;
	}
	return -1;
}

[[nodiscard]] std::string EncodeBase64(const std::uint8_t* Data, std::size_t Size)
{
	std::string Out;
	Out.reserve(((Size + 2) / 3) * 4);
	std::size_t I = 0;
	while (I + 2 < Size)
	{
		const std::uint32_t N =
			(static_cast<std::uint32_t>(Data[I]) << 16)
			| (static_cast<std::uint32_t>(Data[I + 1]) << 8)
			| static_cast<std::uint32_t>(Data[I + 2]);
		Out.push_back(kBase64Table[(N >> 18) & 63]);
		Out.push_back(kBase64Table[(N >> 12) & 63]);
		Out.push_back(kBase64Table[(N >> 6) & 63]);
		Out.push_back(kBase64Table[N & 63]);
		I += 3;
	}
	if (I < Size)
	{
		std::uint32_t N = static_cast<std::uint32_t>(Data[I]) << 16;
		Out.push_back(kBase64Table[(N >> 18) & 63]);
		if (I + 1 < Size)
		{
			N |= static_cast<std::uint32_t>(Data[I + 1]) << 8;
			Out.push_back(kBase64Table[(N >> 12) & 63]);
			Out.push_back(kBase64Table[(N >> 6) & 63]);
			Out.push_back('=');
		}
		else
		{
			Out.push_back(kBase64Table[(N >> 12) & 63]);
			Out.push_back('=');
			Out.push_back('=');
		}
	}
	return Out;
}

[[nodiscard]] bool DecodeBase64(const std::string& Text, std::vector<std::uint8_t>& Out)
{
	Out.clear();
	if (Text.empty())
	{
		return true;
	}
	if ((Text.size() % 4) != 0)
	{
		return false;
	}
	Out.reserve(Text.size() / 4 * 3);
	for (std::size_t I = 0; I < Text.size(); I += 4)
	{
		const int A = Base64DecodeValue(Text[I]);
		const int B = Base64DecodeValue(Text[I + 1]);
		const int C = Text[I + 2] == '=' ? 0 : Base64DecodeValue(Text[I + 2]);
		const int D = Text[I + 3] == '=' ? 0 : Base64DecodeValue(Text[I + 3]);
		if (A < 0 || B < 0 || (Text[I + 2] != '=' && C < 0) || (Text[I + 3] != '=' && D < 0))
		{
			Out.clear();
			return false;
		}
		const std::uint32_t N =
			(static_cast<std::uint32_t>(A) << 18)
			| (static_cast<std::uint32_t>(B) << 12)
			| (static_cast<std::uint32_t>(C) << 6)
			| static_cast<std::uint32_t>(D);
		Out.push_back(static_cast<std::uint8_t>((N >> 16) & 0xFF));
		if (Text[I + 2] != '=')
		{
			Out.push_back(static_cast<std::uint8_t>((N >> 8) & 0xFF));
		}
		if (Text[I + 3] != '=')
		{
			Out.push_back(static_cast<std::uint8_t>(N & 0xFF));
		}
	}
	return true;
}

template <typename T>
[[nodiscard]] FJsonValue EncodeBlob(const std::vector<T>& Values)
{
	if (Values.empty())
	{
		return FJsonValue::String({});
	}
	const auto* Bytes = reinterpret_cast<const std::uint8_t*>(Values.data());
	return FJsonValue::String(EncodeBase64(Bytes, Values.size() * sizeof(T)));
}

template <typename T>
[[nodiscard]] bool DecodeBlob(const FJsonValue& Value, std::vector<T>& Out)
{
	Out.clear();
	const std::string Encoded = Value.AsString();
	std::vector<std::uint8_t> Bytes;
	if (!DecodeBase64(Encoded, Bytes))
	{
		return false;
	}
	if ((Bytes.size() % sizeof(T)) != 0)
	{
		return false;
	}
	Out.resize(Bytes.size() / sizeof(T));
	if (!Out.empty())
	{
		std::memcpy(Out.data(), Bytes.data(), Bytes.size());
	}
	return true;
}

[[nodiscard]] const char* DimensionToString(ETextureDimension Dim)
{
	switch (Dim)
	{
	case ETextureDimension::Tex3D: return "Tex3D";
	case ETextureDimension::Cube: return "Cube";
	case ETextureDimension::CubeArray: return "CubeArray";
	case ETextureDimension::Tex2DArray: return "Tex2DArray";
	case ETextureDimension::Tex2D:
	default: return "Tex2D";
	}
}

[[nodiscard]] ETextureDimension DimensionFromString(const std::string& Name)
{
	if (Name == "Tex3D")
	{
		return ETextureDimension::Tex3D;
	}
	if (Name == "Cube")
	{
		return ETextureDimension::Cube;
	}
	if (Name == "CubeArray")
	{
		return ETextureDimension::CubeArray;
	}
	if (Name == "Tex2DArray")
	{
		return ETextureDimension::Tex2DArray;
	}
	return ETextureDimension::Tex2D;
}

[[nodiscard]] const char* PixelFormatToString(ETexturePixelFormat Format)
{
	switch (Format)
	{
	case ETexturePixelFormat::R8: return "R8";
	case ETexturePixelFormat::RG8: return "RG8";
	case ETexturePixelFormat::RGB8: return "RGB8";
	case ETexturePixelFormat::RGBA8: return "RGBA8";
	case ETexturePixelFormat::RGBA16F: return "RGBA16F";
	case ETexturePixelFormat::RGBA32F: return "RGBA32F";
	case ETexturePixelFormat::BlockCompressed: return "BlockCompressed";
	case ETexturePixelFormat::Unknown:
	default: return "Unknown";
	}
}

[[nodiscard]] ETexturePixelFormat PixelFormatFromString(const std::string& Name)
{
	if (Name == "R8")
	{
		return ETexturePixelFormat::R8;
	}
	if (Name == "RG8")
	{
		return ETexturePixelFormat::RG8;
	}
	if (Name == "RGB8")
	{
		return ETexturePixelFormat::RGB8;
	}
	if (Name == "RGBA8")
	{
		return ETexturePixelFormat::RGBA8;
	}
	if (Name == "RGBA16F")
	{
		return ETexturePixelFormat::RGBA16F;
	}
	if (Name == "RGBA32F")
	{
		return ETexturePixelFormat::RGBA32F;
	}
	if (Name == "BlockCompressed")
	{
		return ETexturePixelFormat::BlockCompressed;
	}
	return ETexturePixelFormat::Unknown;
}

[[nodiscard]] bool WriteTexture(const UTexture& Tex, FJsonValue& Cpu)
{
	Cpu.SetField("dimension", FJsonValue::String(DimensionToString(Tex.GetDimension())));
	Cpu.SetField("format", FJsonValue::String(PixelFormatToString(Tex.GetPixelFormat())));
	Cpu.SetField("width", FJsonValue::Number(static_cast<std::int64_t>(Tex.GetWidth())));
	Cpu.SetField("height", FJsonValue::Number(static_cast<std::int64_t>(Tex.GetHeight())));
	Cpu.SetField("depth", FJsonValue::Number(static_cast<std::int64_t>(Tex.GetDepth())));
	Cpu.SetField("arrayLayers", FJsonValue::Number(static_cast<std::int64_t>(Tex.GetArrayLayers())));
	Cpu.SetField("mipCount", FJsonValue::Number(static_cast<std::int64_t>(Tex.GetMipCount())));
	Cpu.SetField("srgb", FJsonValue::Bool(Tex.IsSRGB()));
	Cpu.SetField("pixelsBase64", EncodeBlob(Tex.GetPixels()));
	return true;
}

[[nodiscard]] bool ReadTexture(UTexture& Tex, const FJsonValue& Cpu)
{
	std::vector<std::uint8_t> Pixels;
	if (!DecodeBlob(Cpu.GetField("pixelsBase64"), Pixels))
	{
		MAHO_CORE_ERROR("casset: texture pixelsBase64 decode failed for '{}'", Tex.GetName());
		return false;
	}
	Tex.SetCpuImage(
		DimensionFromString(Cpu.GetField("dimension").AsString("Tex2D")),
		PixelFormatFromString(Cpu.GetField("format").AsString("RGBA8")),
		static_cast<std::uint32_t>(Cpu.GetField("width").AsInt64(0)),
		static_cast<std::uint32_t>(Cpu.GetField("height").AsInt64(0)),
		static_cast<std::uint32_t>(Cpu.GetField("depth").AsInt64(1)),
		static_cast<std::uint32_t>(Cpu.GetField("arrayLayers").AsInt64(1)),
		static_cast<std::uint32_t>(Cpu.GetField("mipCount").AsInt64(1)),
		Cpu.GetField("srgb").AsBool(true),
		std::move(Pixels));
	return true;
}

[[nodiscard]] bool WriteMesh(const UStaticMesh& Mesh, FJsonValue& Cpu)
{
	Cpu.SetField("material", FJsonValue::String(Mesh.GetMaterial().ToString()));
	Cpu.SetField("positionsBase64", EncodeBlob(Mesh.GetPositions()));
	Cpu.SetField("normalsBase64", EncodeBlob(Mesh.GetNormals()));
	Cpu.SetField("uvsBase64", EncodeBlob(Mesh.GetUVs()));
	Cpu.SetField("indicesBase64", EncodeBlob(Mesh.GetIndices()));
	return true;
}

[[nodiscard]] bool ReadMesh(UStaticMesh& Mesh, const FJsonValue& Cpu)
{
	std::vector<float> Positions;
	std::vector<float> Normals;
	std::vector<float> UVs;
	std::vector<std::uint32_t> Indices;
	if (!DecodeBlob(Cpu.GetField("positionsBase64"), Positions)
		|| !DecodeBlob(Cpu.GetField("normalsBase64"), Normals)
		|| !DecodeBlob(Cpu.GetField("uvsBase64"), UVs)
		|| !DecodeBlob(Cpu.GetField("indicesBase64"), Indices))
	{
		MAHO_CORE_ERROR("casset: mesh blob decode failed for '{}'", Mesh.GetName());
		return false;
	}
	Mesh.SetCpuGeometry(std::move(Positions), std::move(Normals), std::move(UVs), std::move(Indices));
	const std::string MatPath = Cpu.GetField("material").AsString();
	if (!MatPath.empty())
	{
		FSoftObjectPath Soft;
		if (Soft.TrySetPath(MatPath))
		{
			Mesh.SetMaterial(std::move(Soft));
		}
	}
	return true;
}

[[nodiscard]] bool WriteMaterial(const UMaterial& Mat, FJsonValue& Cpu)
{
	Cpu.SetField("baseColorTexture", FJsonValue::String(Mat.GetBaseColorTexture().ToString()));
	Cpu.SetField("normalTexture", FJsonValue::String(Mat.GetNormalTexture().ToString()));
	Cpu.SetField("metallicRoughnessTexture", FJsonValue::String(Mat.GetMetallicRoughnessTexture().ToString()));
	Cpu.SetField("occlusionTexture", FJsonValue::String(Mat.GetOcclusionTexture().ToString()));
	Cpu.SetField("emissiveTexture", FJsonValue::String(Mat.GetEmissiveTexture().ToString()));
	FJsonValue Factor = FJsonValue::Array();
	Factor.AddElement(FJsonValue::Number(Mat.BaseColorFactor[0]));
	Factor.AddElement(FJsonValue::Number(Mat.BaseColorFactor[1]));
	Factor.AddElement(FJsonValue::Number(Mat.BaseColorFactor[2]));
	Factor.AddElement(FJsonValue::Number(Mat.BaseColorFactor[3]));
	Cpu.SetField("baseColorFactor", Factor);
	Cpu.SetField("metallicFactor", FJsonValue::Number(Mat.MetallicFactor));
	Cpu.SetField("roughnessFactor", FJsonValue::Number(Mat.RoughnessFactor));
	FJsonValue Emissive = FJsonValue::Array();
	Emissive.AddElement(FJsonValue::Number(Mat.EmissiveFactor[0]));
	Emissive.AddElement(FJsonValue::Number(Mat.EmissiveFactor[1]));
	Emissive.AddElement(FJsonValue::Number(Mat.EmissiveFactor[2]));
	Cpu.SetField("emissiveFactor", Emissive);
	return true;
}

[[nodiscard]] bool ReadMaterial(UMaterial& Mat, const FJsonValue& Cpu)
{
	auto SetSoft = [](const FJsonValue& Field, auto Setter)
	{
		const std::string Path = Field.AsString();
		if (Path.empty())
		{
			return;
		}
		FSoftObjectPath Soft;
		if (Soft.TrySetPath(Path))
		{
			Setter(std::move(Soft));
		}
	};
	SetSoft(Cpu.GetField("baseColorTexture"), [&](FSoftObjectPath P) { Mat.SetBaseColorTexture(std::move(P)); });
	SetSoft(Cpu.GetField("normalTexture"), [&](FSoftObjectPath P) { Mat.SetNormalTexture(std::move(P)); });
	SetSoft(
		Cpu.GetField("metallicRoughnessTexture"),
		[&](FSoftObjectPath P) { Mat.SetMetallicRoughnessTexture(std::move(P)); });
	SetSoft(Cpu.GetField("occlusionTexture"), [&](FSoftObjectPath P) { Mat.SetOcclusionTexture(std::move(P)); });
	SetSoft(Cpu.GetField("emissiveTexture"), [&](FSoftObjectPath P) { Mat.SetEmissiveTexture(std::move(P)); });

	const FJsonValue Factor = Cpu.GetField("baseColorFactor");
	if (Factor.IsArray() && Factor.GetArraySize() >= 4)
	{
		Mat.BaseColorFactor[0] = Factor.GetElement(0).AsFloat(1.f);
		Mat.BaseColorFactor[1] = Factor.GetElement(1).AsFloat(1.f);
		Mat.BaseColorFactor[2] = Factor.GetElement(2).AsFloat(1.f);
		Mat.BaseColorFactor[3] = Factor.GetElement(3).AsFloat(1.f);
	}
	Mat.MetallicFactor = Cpu.GetField("metallicFactor").AsFloat(0.f);
	Mat.RoughnessFactor = Cpu.GetField("roughnessFactor").AsFloat(1.f);
	const FJsonValue Emissive = Cpu.GetField("emissiveFactor");
	if (Emissive.IsArray() && Emissive.GetArraySize() >= 3)
	{
		Mat.EmissiveFactor[0] = Emissive.GetElement(0).AsFloat(0.f);
		Mat.EmissiveFactor[1] = Emissive.GetElement(1).AsFloat(0.f);
		Mat.EmissiveFactor[2] = Emissive.GetElement(2).AsFloat(0.f);
	}
	return true;
}

[[nodiscard]] bool WriteSkeleton(const USkeleton& Skeleton, FJsonValue& Cpu)
{
	FJsonValue Bones = FJsonValue::Array();
	for (const FSkeletonBone& Bone : Skeleton.GetBones())
	{
		FJsonValue Entry = FJsonValue::Object();
		Entry.SetField("name", FJsonValue::String(Bone.Name));
		Entry.SetField("parent", FJsonValue::Number(static_cast<std::int64_t>(Bone.ParentIndex)));
		FJsonValue Matrix = FJsonValue::Array();
		for (int I = 0; I < 16; ++I)
		{
			Matrix.AddElement(FJsonValue::Number(Bone.BindLocal[I]));
		}
		Entry.SetField("bindLocal", Matrix);
		Bones.AddElement(Entry);
	}
	Cpu.SetField("bones", Bones);
	return true;
}

[[nodiscard]] bool ReadSkeleton(USkeleton& Skeleton, const FJsonValue& Cpu)
{
	std::vector<FSkeletonBone> Bones;
	const FJsonValue Arr = Cpu.GetField("bones");
	if (Arr.IsArray())
	{
		Bones.reserve(Arr.GetArraySize());
		for (std::size_t I = 0; I < Arr.GetArraySize(); ++I)
		{
			const FJsonValue Entry = Arr.GetElement(I);
			FSkeletonBone Bone;
			Bone.Name = Entry.GetField("name").AsString();
			Bone.ParentIndex = static_cast<std::int32_t>(Entry.GetField("parent").AsInt64(-1));
			const FJsonValue Matrix = Entry.GetField("bindLocal");
			if (Matrix.IsArray())
			{
				const std::size_t N = Matrix.GetArraySize() < 16 ? Matrix.GetArraySize() : 16;
				for (std::size_t M = 0; M < N; ++M)
				{
					Bone.BindLocal[M] = Matrix.GetElement(M).AsFloat(0.f);
				}
			}
			Bones.push_back(std::move(Bone));
		}
	}
	Skeleton.SetBones(std::move(Bones));
	return true;
}

[[nodiscard]] bool WriteAnimation(const UAnimation& Anim, FJsonValue& Cpu)
{
	Cpu.SetField("skeleton", FJsonValue::String(Anim.GetSkeleton().ToString()));
	Cpu.SetField("duration", FJsonValue::Number(Anim.GetDurationSeconds()));
	FJsonValue Tracks = FJsonValue::Array();
	for (const FAnimationTrack& Track : Anim.GetTracks())
	{
		FJsonValue TrackJson = FJsonValue::Object();
		TrackJson.SetField("bone", FJsonValue::String(Track.TargetBoneName));
		FJsonValue Keys = FJsonValue::Array();
		for (const FAnimationKey& Key : Track.Keys)
		{
			FJsonValue KeyJson = FJsonValue::Object();
			KeyJson.SetField("t", FJsonValue::Number(Key.Time));
			FJsonValue Tr = FJsonValue::Array();
			Tr.AddElement(FJsonValue::Number(Key.Translation[0]));
			Tr.AddElement(FJsonValue::Number(Key.Translation[1]));
			Tr.AddElement(FJsonValue::Number(Key.Translation[2]));
			KeyJson.SetField("translation", Tr);
			FJsonValue Rot = FJsonValue::Array();
			Rot.AddElement(FJsonValue::Number(Key.Rotation[0]));
			Rot.AddElement(FJsonValue::Number(Key.Rotation[1]));
			Rot.AddElement(FJsonValue::Number(Key.Rotation[2]));
			Rot.AddElement(FJsonValue::Number(Key.Rotation[3]));
			KeyJson.SetField("rotation", Rot);
			FJsonValue Sc = FJsonValue::Array();
			Sc.AddElement(FJsonValue::Number(Key.Scale[0]));
			Sc.AddElement(FJsonValue::Number(Key.Scale[1]));
			Sc.AddElement(FJsonValue::Number(Key.Scale[2]));
			KeyJson.SetField("scale", Sc);
			Keys.AddElement(KeyJson);
		}
		TrackJson.SetField("keys", Keys);
		Tracks.AddElement(TrackJson);
	}
	Cpu.SetField("tracks", Tracks);
	return true;
}

[[nodiscard]] bool ReadAnimation(UAnimation& Anim, const FJsonValue& Cpu)
{
	const std::string SkelPath = Cpu.GetField("skeleton").AsString();
	if (!SkelPath.empty())
	{
		FSoftObjectPath Soft;
		if (Soft.TrySetPath(SkelPath))
		{
			Anim.SetSkeleton(std::move(Soft));
		}
	}
	Anim.SetDurationSeconds(Cpu.GetField("duration").AsFloat(0.f));
	std::vector<FAnimationTrack> Tracks;
	const FJsonValue TracksJson = Cpu.GetField("tracks");
	if (TracksJson.IsArray())
	{
		Tracks.reserve(TracksJson.GetArraySize());
		for (std::size_t I = 0; I < TracksJson.GetArraySize(); ++I)
		{
			const FJsonValue TrackJson = TracksJson.GetElement(I);
			FAnimationTrack Track;
			Track.TargetBoneName = TrackJson.GetField("bone").AsString();
			const FJsonValue KeysJson = TrackJson.GetField("keys");
			if (KeysJson.IsArray())
			{
				Track.Keys.reserve(KeysJson.GetArraySize());
				for (std::size_t K = 0; K < KeysJson.GetArraySize(); ++K)
				{
					const FJsonValue KeyJson = KeysJson.GetElement(K);
					FAnimationKey Key;
					Key.Time = KeyJson.GetField("t").AsFloat(0.f);
					const FJsonValue Tr = KeyJson.GetField("translation");
					if (Tr.IsArray() && Tr.GetArraySize() >= 3)
					{
						Key.Translation[0] = Tr.GetElement(0).AsFloat(0.f);
						Key.Translation[1] = Tr.GetElement(1).AsFloat(0.f);
						Key.Translation[2] = Tr.GetElement(2).AsFloat(0.f);
					}
					const FJsonValue Rot = KeyJson.GetField("rotation");
					if (Rot.IsArray() && Rot.GetArraySize() >= 4)
					{
						Key.Rotation[0] = Rot.GetElement(0).AsFloat(0.f);
						Key.Rotation[1] = Rot.GetElement(1).AsFloat(0.f);
						Key.Rotation[2] = Rot.GetElement(2).AsFloat(0.f);
						Key.Rotation[3] = Rot.GetElement(3).AsFloat(1.f);
					}
					const FJsonValue Sc = KeyJson.GetField("scale");
					if (Sc.IsArray() && Sc.GetArraySize() >= 3)
					{
						Key.Scale[0] = Sc.GetElement(0).AsFloat(1.f);
						Key.Scale[1] = Sc.GetElement(1).AsFloat(1.f);
						Key.Scale[2] = Sc.GetElement(2).AsFloat(1.f);
					}
					Track.Keys.push_back(Key);
				}
			}
			Tracks.push_back(std::move(Track));
		}
	}
	Anim.SetTracks(std::move(Tracks));
	return true;
}

} // namespace

bool WriteCpuPayload(const UResource& Resource, FJsonValue& InOutEntry)
{
	FJsonValue Cpu = FJsonValue::Object();
	bool bOk = false;

	if (const UTexture* Tex = dynamic_cast<const UTexture*>(&Resource))
	{
		bOk = WriteTexture(*Tex, Cpu);
	}
	else if (const UStaticMesh* Mesh = dynamic_cast<const UStaticMesh*>(&Resource))
	{
		bOk = WriteMesh(*Mesh, Cpu);
	}
	else if (const UMaterial* Mat = dynamic_cast<const UMaterial*>(&Resource))
	{
		bOk = WriteMaterial(*Mat, Cpu);
	}
	else if (const USkeleton* Skeleton = dynamic_cast<const USkeleton*>(&Resource))
	{
		bOk = WriteSkeleton(*Skeleton, Cpu);
	}
	else if (const UAnimation* Anim = dynamic_cast<const UAnimation*>(&Resource))
	{
		bOk = WriteAnimation(*Anim, Cpu);
	}
	else if (const UAnimationGraph* Graph = dynamic_cast<const UAnimationGraph*>(&Resource))
	{
		Cpu.SetField("documentJson", FJsonValue::String(Graph->GetDocumentJson()));
		bOk = true;
	}
	else if (const UPrefab* Prefab = dynamic_cast<const UPrefab*>(&Resource))
	{
		Cpu.SetField("documentJson", FJsonValue::String(Prefab->GetDocumentJson()));
		bOk = true;
	}
	else
	{
		MAHO_CORE_WARN("casset: no CPU writer for '{}'", Resource.GetName());
		bOk = true;
	}

	if (bOk)
	{
		InOutEntry.SetField("cpu", Cpu);
		if (!Resource.GetSourcePath().empty())
		{
			InOutEntry.SetField("importSource", FJsonValue::String(Resource.GetSourcePath()));
		}
	}
	return bOk;
}

bool ReadCpuPayload(UResource& Resource, const FJsonValue& Entry)
{
	if (!Entry.HasField("cpu") || !Entry.GetField("cpu").IsObject())
	{
		MAHO_CORE_ERROR("casset: missing cpu payload for '{}'", Resource.GetName());
		return false;
	}
	const FJsonValue Cpu = Entry.GetField("cpu");
	bool bOk = false;

	if (UTexture* Tex = dynamic_cast<UTexture*>(&Resource))
	{
		bOk = ReadTexture(*Tex, Cpu);
	}
	else if (UStaticMesh* Mesh = dynamic_cast<UStaticMesh*>(&Resource))
	{
		bOk = ReadMesh(*Mesh, Cpu);
	}
	else if (UMaterial* Mat = dynamic_cast<UMaterial*>(&Resource))
	{
		bOk = ReadMaterial(*Mat, Cpu);
	}
	else if (USkeleton* Skeleton = dynamic_cast<USkeleton*>(&Resource))
	{
		bOk = ReadSkeleton(*Skeleton, Cpu);
	}
	else if (UAnimation* Anim = dynamic_cast<UAnimation*>(&Resource))
	{
		bOk = ReadAnimation(*Anim, Cpu);
	}
	else if (UAnimationGraph* Graph = dynamic_cast<UAnimationGraph*>(&Resource))
	{
		Graph->SetDocumentJson(Cpu.GetField("documentJson").AsString());
		bOk = true;
	}
	else if (UPrefab* Prefab = dynamic_cast<UPrefab*>(&Resource))
	{
		Prefab->SetDocumentJson(Cpu.GetField("documentJson").AsString());
		bOk = true;
	}
	else
	{
		bOk = true;
	}

	if (bOk)
	{
		Resource.MarkCpuReady();
		Resource.ClearDirty();
	}
	return bOk;
}

} // namespace ResourceCasset
} // namespace Maho
