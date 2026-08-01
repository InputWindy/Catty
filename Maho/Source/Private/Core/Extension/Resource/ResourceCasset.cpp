#include "ResourceCasset.h"
#include "TextureImageCodec.h"

#include <Core/System/Compression.h>
#include <Core/System/Log.h>
#include <Core/Object/Package.h>
#include <Core/Object/SoftObjectPath.h>

#include <cstring>
#include <limits>
#include <unordered_map>

namespace Maho
{
namespace ResourceCasset
{
namespace
{

class FByteWriter
{
public:
	std::vector<std::uint8_t> Bytes;

	void WriteU8(std::uint8_t V) { Bytes.push_back(V); }

	void WriteU16(std::uint16_t V)
	{
		Bytes.push_back(static_cast<std::uint8_t>(V & 0xFF));
		Bytes.push_back(static_cast<std::uint8_t>((V >> 8) & 0xFF));
	}

	void WriteU32(std::uint32_t V)
	{
		Bytes.push_back(static_cast<std::uint8_t>(V & 0xFF));
		Bytes.push_back(static_cast<std::uint8_t>((V >> 8) & 0xFF));
		Bytes.push_back(static_cast<std::uint8_t>((V >> 16) & 0xFF));
		Bytes.push_back(static_cast<std::uint8_t>((V >> 24) & 0xFF));
	}

	void WriteI32(std::int32_t V) { WriteU32(static_cast<std::uint32_t>(V)); }

	void WriteF32(float V)
	{
		std::uint32_t Bits = 0;
		std::memcpy(&Bits, &V, sizeof(Bits));
		WriteU32(Bits);
	}

	void WriteBytes(const void* Data, std::size_t Size)
	{
		if (Size == 0)
		{
			return;
		}
		const auto* Ptr = static_cast<const std::uint8_t*>(Data);
		Bytes.insert(Bytes.end(), Ptr, Ptr + Size);
	}

	void WriteString(const std::string& Text)
	{
		if (Text.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
		{
			WriteU32(0);
			return;
		}
		WriteU32(static_cast<std::uint32_t>(Text.size()));
		WriteBytes(Text.data(), Text.size());
	}

	void PadTo4()
	{
		while ((Bytes.size() & 3u) != 0)
		{
			Bytes.push_back(0);
		}
	}
};

class FByteReader
{
public:
	const std::uint8_t* Data = nullptr;
	std::size_t Size = 0;
	std::size_t Pos = 0;

	[[nodiscard]] bool Remaining(std::size_t N) const { return Pos + N <= Size; }

	[[nodiscard]] bool ReadU8(std::uint8_t& Out)
	{
		if (!Remaining(1))
		{
			return false;
		}
		Out = Data[Pos++];
		return true;
	}

	[[nodiscard]] bool ReadU16(std::uint16_t& Out)
	{
		if (!Remaining(2))
		{
			return false;
		}
		Out = static_cast<std::uint16_t>(Data[Pos] | (Data[Pos + 1] << 8));
		Pos += 2;
		return true;
	}

	[[nodiscard]] bool ReadU32(std::uint32_t& Out)
	{
		if (!Remaining(4))
		{
			return false;
		}
		Out = static_cast<std::uint32_t>(Data[Pos])
			| (static_cast<std::uint32_t>(Data[Pos + 1]) << 8)
			| (static_cast<std::uint32_t>(Data[Pos + 2]) << 16)
			| (static_cast<std::uint32_t>(Data[Pos + 3]) << 24);
		Pos += 4;
		return true;
	}

	[[nodiscard]] bool ReadI32(std::int32_t& Out)
	{
		std::uint32_t U = 0;
		if (!ReadU32(U))
		{
			return false;
		}
		Out = static_cast<std::int32_t>(U);
		return true;
	}

	[[nodiscard]] bool ReadF32(float& Out)
	{
		std::uint32_t Bits = 0;
		if (!ReadU32(Bits))
		{
			return false;
		}
		std::memcpy(&Out, &Bits, sizeof(Out));
		return true;
	}

	[[nodiscard]] bool ReadBytes(void* Dest, std::size_t N)
	{
		if (!Remaining(N))
		{
			return false;
		}
		std::memcpy(Dest, Data + Pos, N);
		Pos += N;
		return true;
	}

	[[nodiscard]] bool ReadString(std::string& Out)
	{
		std::uint32_t Len = 0;
		if (!ReadU32(Len))
		{
			return false;
		}
		if (!Remaining(Len))
		{
			return false;
		}
		Out.assign(reinterpret_cast<const char*>(Data + Pos), Len);
		Pos += Len;
		return true;
	}

	[[nodiscard]] bool Skip(std::size_t N)
	{
		if (!Remaining(N))
		{
			return false;
		}
		Pos += N;
		return true;
	}

	[[nodiscard]] bool SkipPadTo4()
	{
		const std::size_t Rem = Pos & 3u;
		if (Rem == 0)
		{
			return true;
		}
		return Skip(4 - Rem);
	}
};

void AppendChunk(FByteWriter& Doc, std::uint32_t Tag, std::uint32_t ChunkFlags, const std::vector<std::uint8_t>& Payload)
{
	Doc.WriteU32(Tag);
	Doc.WriteU32(static_cast<std::uint32_t>(Payload.size()));
	Doc.WriteU32(ChunkFlags);
	Doc.WriteBytes(Payload.data(), Payload.size());
	Doc.PadTo4();
}

[[nodiscard]] bool WriteTextureCpu(const UTexture& Tex, FByteWriter& W)
{
	std::vector<std::uint8_t> EncodedFallback;
	const std::uint8_t* Payload = nullptr;
	std::size_t PayloadSize = 0;
	std::uint8_t PayloadKind = kTexturePayloadRaw;
	std::string EncodedHint;

	if (Tex.HasSerializedSource())
	{
		PayloadKind = kTexturePayloadEncoded;
		EncodedHint = Tex.GetSerializedSourceHint();
		Payload = Tex.GetSerializedSourceBytes().data();
		PayloadSize = Tex.GetSerializedSourceBytes().size();
	}
	else if (Tex.GetPixelFormat() == ETexturePixelFormat::RGBA8 && !Tex.GetPixels().empty())
	{
		if (!TextureImageCodec::EncodePngToMemory(Tex, EncodedFallback) || EncodedFallback.empty())
		{
			MAHO_CORE_ERROR("casset: PNG encode fallback failed for texture '{}'", Tex.GetName());
			return false;
		}
		PayloadKind = kTexturePayloadEncoded;
		EncodedHint = ".png";
		Payload = EncodedFallback.data();
		PayloadSize = EncodedFallback.size();
	}
	else
	{
		Payload = Tex.GetPixels().data();
		PayloadSize = Tex.GetPixels().size();
	}

	W.WriteU16(kTextureCpuLayoutEncoded);
	W.WriteU16(static_cast<std::uint16_t>(Tex.GetDimension()));
	W.WriteU16(static_cast<std::uint16_t>(Tex.GetPixelFormat()));
	W.WriteU16(0); // pad
	W.WriteU32(Tex.GetWidth());
	W.WriteU32(Tex.GetHeight());
	W.WriteU32(Tex.GetDepth());
	W.WriteU32(Tex.GetArrayLayers());
	W.WriteU32(Tex.GetMipCount());
	W.WriteU8(Tex.IsSRGB() ? 1 : 0);
	W.WriteU8(PayloadKind);
	W.WriteU16(0);
	if (PayloadKind == kTexturePayloadEncoded)
	{
		W.WriteString(EncodedHint);
	}
	W.WriteU32(static_cast<std::uint32_t>(PayloadSize));
	W.WriteBytes(Payload, PayloadSize);
	return true;
}

[[nodiscard]] bool ReadTextureCpu(UTexture& Tex, FByteReader& R)
{
	std::uint16_t Layout = 0;
	std::uint16_t Dim = 0;
	std::uint16_t Format = 0;
	std::uint16_t Pad0 = 0;
	if (!R.ReadU16(Layout) || !R.ReadU16(Dim) || !R.ReadU16(Format) || !R.ReadU16(Pad0))
	{
		return false;
	}
	if (Layout != kTextureCpuLayoutRaw && Layout != kTextureCpuLayoutEncoded)
	{
		return false;
	}

	std::uint32_t W = 0, H = 0, D = 0, Layers = 0, Mips = 0, PayloadBytes = 0;
	std::uint8_t Srgb = 0, PayloadKind = kTexturePayloadRaw;
	std::uint16_t Pad2 = 0;
	if (!R.ReadU32(W) || !R.ReadU32(H) || !R.ReadU32(D) || !R.ReadU32(Layers) || !R.ReadU32(Mips)
		|| !R.ReadU8(Srgb) || !R.ReadU8(PayloadKind) || !R.ReadU16(Pad2))
	{
		return false;
	}

	std::string EncodedHint;
	if (Layout == kTextureCpuLayoutEncoded && PayloadKind == kTexturePayloadEncoded)
	{
		if (!R.ReadString(EncodedHint))
		{
			return false;
		}
	}

	if (!R.ReadU32(PayloadBytes))
	{
		return false;
	}
	std::vector<std::uint8_t> Payload(PayloadBytes);
	if (PayloadBytes > 0 && !R.ReadBytes(Payload.data(), PayloadBytes))
	{
		return false;
	}

	if (Layout == kTextureCpuLayoutEncoded && PayloadKind == kTexturePayloadEncoded)
	{
		FDecodedImage Image;
		const std::string Hint = EncodedHint.empty() ? Tex.GetSourcePath() : EncodedHint;
		if (!TextureImageCodec::DecodeFromMemory(Payload.data(), Payload.size(), Hint, Image))
		{
			MAHO_CORE_ERROR("casset: encoded texture decode failed for '{}'", Tex.GetName());
			return false;
		}
		Image.bSRGB = Srgb != 0;
		if (!TextureImageCodec::ApplyDecodedToTexture(Tex, std::move(Image)))
		{
			return false;
		}
		Tex.SetSerializedSource(Hint, std::move(Payload));
		return true;
	}

	Tex.SetCpuImage(
		static_cast<ETextureDimension>(Dim),
		static_cast<ETexturePixelFormat>(Format),
		W,
		H,
		D,
		Layers,
		Mips,
		Srgb != 0,
		std::move(Payload));
	return true;
}

template <typename T>
void WriteBlob(FByteWriter& W, const std::vector<T>& Values)
{
	const std::uint32_t ByteCount = static_cast<std::uint32_t>(Values.size() * sizeof(T));
	W.WriteU32(ByteCount);
	if (ByteCount > 0)
	{
		W.WriteBytes(Values.data(), ByteCount);
	}
}

template <typename T>
[[nodiscard]] bool ReadBlob(FByteReader& R, std::vector<T>& Out)
{
	Out.clear();
	std::uint32_t ByteCount = 0;
	if (!R.ReadU32(ByteCount))
	{
		return false;
	}
	if (ByteCount == 0)
	{
		return true;
	}
	if ((ByteCount % sizeof(T)) != 0 || !R.Remaining(ByteCount))
	{
		return false;
	}
	Out.resize(ByteCount / sizeof(T));
	return R.ReadBytes(Out.data(), ByteCount);
}

[[nodiscard]] bool WriteMeshCpu(const UStaticMesh& Mesh, FByteWriter& W)
{
	W.WriteU16(kCpuLayoutVersion);
	W.WriteU16(0);
	W.WriteString(Mesh.GetMaterial().ToString());
	WriteBlob(W, Mesh.GetPositions());
	WriteBlob(W, Mesh.GetNormals());
	WriteBlob(W, Mesh.GetUVs());
	WriteBlob(W, Mesh.GetIndices());
	return true;
}

[[nodiscard]] bool ReadMeshCpu(UStaticMesh& Mesh, FByteReader& R)
{
	std::uint16_t Layout = 0;
	std::uint16_t Pad = 0;
	std::string MatPath;
	if (!R.ReadU16(Layout) || Layout != kCpuLayoutVersion || !R.ReadU16(Pad) || !R.ReadString(MatPath))
	{
		return false;
	}
	std::vector<float> Positions, Normals, UVs;
	std::vector<std::uint32_t> Indices;
	if (!ReadBlob(R, Positions) || !ReadBlob(R, Normals) || !ReadBlob(R, UVs) || !ReadBlob(R, Indices))
	{
		return false;
	}
	Mesh.SetCpuGeometry(std::move(Positions), std::move(Normals), std::move(UVs), std::move(Indices));
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

[[nodiscard]] bool WriteMaterialCpu(const UMaterial& Mat, FByteWriter& W)
{
	W.WriteU16(kCpuLayoutVersion);
	W.WriteU16(0);
	W.WriteString(Mat.GetBaseColorTexture().ToString());
	W.WriteString(Mat.GetNormalTexture().ToString());
	W.WriteString(Mat.GetMetallicRoughnessTexture().ToString());
	W.WriteString(Mat.GetOcclusionTexture().ToString());
	W.WriteString(Mat.GetEmissiveTexture().ToString());
	for (int I = 0; I < 4; ++I)
	{
		W.WriteF32(Mat.BaseColorFactor[I]);
	}
	W.WriteF32(Mat.MetallicFactor);
	W.WriteF32(Mat.RoughnessFactor);
	for (int I = 0; I < 3; ++I)
	{
		W.WriteF32(Mat.EmissiveFactor[I]);
	}
	return true;
}

[[nodiscard]] bool ReadMaterialCpu(UMaterial& Mat, FByteReader& R)
{
	std::uint16_t Layout = 0;
	std::uint16_t Pad = 0;
	if (!R.ReadU16(Layout) || Layout != kCpuLayoutVersion || !R.ReadU16(Pad))
	{
		return false;
	}
	auto SetSoft = [](const std::string& Path, auto Setter)
	{
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
	std::string Base, Normal, MR, Occ, Emissive;
	if (!R.ReadString(Base) || !R.ReadString(Normal) || !R.ReadString(MR) || !R.ReadString(Occ)
		|| !R.ReadString(Emissive))
	{
		return false;
	}
	SetSoft(Base, [&](FSoftObjectPath P) { Mat.SetBaseColorTexture(std::move(P)); });
	SetSoft(Normal, [&](FSoftObjectPath P) { Mat.SetNormalTexture(std::move(P)); });
	SetSoft(MR, [&](FSoftObjectPath P) { Mat.SetMetallicRoughnessTexture(std::move(P)); });
	SetSoft(Occ, [&](FSoftObjectPath P) { Mat.SetOcclusionTexture(std::move(P)); });
	SetSoft(Emissive, [&](FSoftObjectPath P) { Mat.SetEmissiveTexture(std::move(P)); });
	for (int I = 0; I < 4; ++I)
	{
		if (!R.ReadF32(Mat.BaseColorFactor[I]))
		{
			return false;
		}
	}
	if (!R.ReadF32(Mat.MetallicFactor) || !R.ReadF32(Mat.RoughnessFactor))
	{
		return false;
	}
	for (int I = 0; I < 3; ++I)
	{
		if (!R.ReadF32(Mat.EmissiveFactor[I]))
		{
			return false;
		}
	}
	return true;
}

[[nodiscard]] bool WriteSkeletonCpu(const USkeleton& Skeleton, FByteWriter& W)
{
	W.WriteU16(kCpuLayoutVersion);
	W.WriteU16(0);
	const auto& Bones = Skeleton.GetBones();
	W.WriteU32(static_cast<std::uint32_t>(Bones.size()));
	for (const FSkeletonBone& Bone : Bones)
	{
		W.WriteString(Bone.Name);
		W.WriteI32(Bone.ParentIndex);
		for (int I = 0; I < 16; ++I)
		{
			W.WriteF32(Bone.BindLocal[I]);
		}
	}
	return true;
}

[[nodiscard]] bool ReadSkeletonCpu(USkeleton& Skeleton, FByteReader& R)
{
	std::uint16_t Layout = 0;
	std::uint16_t Pad = 0;
	std::uint32_t Count = 0;
	if (!R.ReadU16(Layout) || Layout != kCpuLayoutVersion || !R.ReadU16(Pad) || !R.ReadU32(Count))
	{
		return false;
	}
	std::vector<FSkeletonBone> Bones;
	Bones.reserve(Count);
	for (std::uint32_t I = 0; I < Count; ++I)
	{
		FSkeletonBone Bone;
		if (!R.ReadString(Bone.Name) || !R.ReadI32(Bone.ParentIndex))
		{
			return false;
		}
		for (int M = 0; M < 16; ++M)
		{
			if (!R.ReadF32(Bone.BindLocal[M]))
			{
				return false;
			}
		}
		Bones.push_back(std::move(Bone));
	}
	Skeleton.SetBones(std::move(Bones));
	return true;
}

[[nodiscard]] bool WriteAnimationCpu(const UAnimation& Anim, FByteWriter& W)
{
	W.WriteU16(kCpuLayoutVersion);
	W.WriteU16(0);
	W.WriteString(Anim.GetSkeleton().ToString());
	W.WriteF32(Anim.GetDurationSeconds());
	const auto& Tracks = Anim.GetTracks();
	W.WriteU32(static_cast<std::uint32_t>(Tracks.size()));
	for (const FAnimationTrack& Track : Tracks)
	{
		W.WriteString(Track.TargetBoneName);
		W.WriteU32(static_cast<std::uint32_t>(Track.Keys.size()));
		for (const FAnimationKey& Key : Track.Keys)
		{
			W.WriteF32(Key.Time);
			W.WriteF32(Key.Translation[0]);
			W.WriteF32(Key.Translation[1]);
			W.WriteF32(Key.Translation[2]);
			W.WriteF32(Key.Rotation[0]);
			W.WriteF32(Key.Rotation[1]);
			W.WriteF32(Key.Rotation[2]);
			W.WriteF32(Key.Rotation[3]);
			W.WriteF32(Key.Scale[0]);
			W.WriteF32(Key.Scale[1]);
			W.WriteF32(Key.Scale[2]);
		}
	}
	return true;
}

[[nodiscard]] bool ReadAnimationCpu(UAnimation& Anim, FByteReader& R)
{
	std::uint16_t Layout = 0;
	std::uint16_t Pad = 0;
	std::string SkelPath;
	float Duration = 0.f;
	std::uint32_t TrackCount = 0;
	if (!R.ReadU16(Layout) || Layout != kCpuLayoutVersion || !R.ReadU16(Pad)
		|| !R.ReadString(SkelPath) || !R.ReadF32(Duration) || !R.ReadU32(TrackCount))
	{
		return false;
	}
	if (!SkelPath.empty())
	{
		FSoftObjectPath Soft;
		if (Soft.TrySetPath(SkelPath))
		{
			Anim.SetSkeleton(std::move(Soft));
		}
	}
	Anim.SetDurationSeconds(Duration);
	std::vector<FAnimationTrack> Tracks;
	Tracks.reserve(TrackCount);
	for (std::uint32_t T = 0; T < TrackCount; ++T)
	{
		FAnimationTrack Track;
		std::uint32_t KeyCount = 0;
		if (!R.ReadString(Track.TargetBoneName) || !R.ReadU32(KeyCount))
		{
			return false;
		}
		Track.Keys.reserve(KeyCount);
		for (std::uint32_t K = 0; K < KeyCount; ++K)
		{
			FAnimationKey Key;
			if (!R.ReadF32(Key.Time)
				|| !R.ReadF32(Key.Translation[0]) || !R.ReadF32(Key.Translation[1]) || !R.ReadF32(Key.Translation[2])
				|| !R.ReadF32(Key.Rotation[0]) || !R.ReadF32(Key.Rotation[1]) || !R.ReadF32(Key.Rotation[2])
				|| !R.ReadF32(Key.Rotation[3])
				|| !R.ReadF32(Key.Scale[0]) || !R.ReadF32(Key.Scale[1]) || !R.ReadF32(Key.Scale[2]))
			{
				return false;
			}
			Track.Keys.push_back(Key);
		}
		Tracks.push_back(std::move(Track));
	}
	Anim.SetTracks(std::move(Tracks));
	return true;
}

[[nodiscard]] bool WriteDocumentJsonCpu(const std::string& DocumentJson, FByteWriter& W)
{
	W.WriteU16(kCpuLayoutVersion);
	W.WriteU16(0);
	W.WriteString(DocumentJson);
	return true;
}

[[nodiscard]] bool ReadDocumentJsonCpu(std::string& OutJson, FByteReader& R)
{
	std::uint16_t Layout = 0;
	std::uint16_t Pad = 0;
	if (!R.ReadU16(Layout) || Layout != kCpuLayoutVersion || !R.ReadU16(Pad) || !R.ReadString(OutJson))
	{
		return false;
	}
	return true;
}

[[nodiscard]] bool WriteObjectRecord(const UObject& Object, FByteWriter& ObjectsPayload)
{
	FByteWriter Record;
	const UResource* Resource = dynamic_cast<const UResource*>(&Object);
	Record.WriteString(Object.GetName());
	Record.WriteU16(static_cast<std::uint16_t>(Resource ? Resource->GetType() : EResourceType::Unknown));
	Record.WriteU16(Resource ? kClassKindResource : kClassKindObject);
	Record.WriteString(Resource ? Resource->GetSourcePath() : std::string{});

	std::vector<UObject*> Referenced;
	Object.GetReferencedObjects(Referenced);
	std::vector<std::uint8_t> CpuBytes;
	std::uint32_t Flags = 0;
	if (Resource)
	{
		if (!WriteCpuPayloadBytes(*Resource, CpuBytes))
		{
			return false;
		}
		if (!CpuBytes.empty())
		{
			Flags |= kRecordHasCpu;
		}
	}
	if (!Referenced.empty())
	{
		Flags |= kRecordHasRefs;
	}
	Record.WriteU32(Flags);
	Record.WriteU32(0);
	Record.WriteU32(0);

	if (Flags & kRecordHasRefs)
	{
		Record.WriteU32(static_cast<std::uint32_t>(Referenced.size()));
		for (UObject* RefObj : Referenced)
		{
			Record.WriteString(RefObj ? RefObj->GetPathName() : std::string{});
		}
	}
	if (Flags & kRecordHasCpu)
	{
		Record.WriteU32(static_cast<std::uint32_t>(CpuBytes.size()));
		Record.WriteBytes(CpuBytes.data(), CpuBytes.size());
	}

	ObjectsPayload.WriteBytes(Record.Bytes.data(), Record.Bytes.size());
	return true;
}

[[nodiscard]] bool ParseObjectRecord(FByteReader& R, FCassetParsedObject& Out)
{
	std::uint16_t TypeU16 = 0;
	std::uint32_t Reserved0 = 0;
	std::uint32_t Reserved1 = 0;
	if (!R.ReadString(Out.Name) || !R.ReadU16(TypeU16) || !R.ReadU16(Out.ClassKind)
		|| !R.ReadString(Out.ImportSource) || !R.ReadU32(Out.RecordFlags)
		|| !R.ReadU32(Reserved0) || !R.ReadU32(Reserved1))
	{
		return false;
	}
	Out.Type = static_cast<EResourceType>(TypeU16);
	if (Out.RecordFlags & kRecordHasRefs)
	{
		std::uint32_t RefCount = 0;
		if (!R.ReadU32(RefCount))
		{
			return false;
		}
		Out.Refs.resize(RefCount);
		for (std::uint32_t I = 0; I < RefCount; ++I)
		{
			if (!R.ReadString(Out.Refs[I]))
			{
				return false;
			}
		}
	}
	if (Out.RecordFlags & kRecordHasCpu)
	{
		std::uint32_t CpuSize = 0;
		if (!R.ReadU32(CpuSize) || !R.Remaining(CpuSize))
		{
			return false;
		}
		Out.CpuBytes.resize(CpuSize);
		if (CpuSize > 0 && !R.ReadBytes(Out.CpuBytes.data(), CpuSize))
		{
			return false;
		}
	}
	if (Out.RecordFlags & kRecordHasExtras)
	{
		std::uint32_t ExtraCount = 0;
		if (!R.ReadU32(ExtraCount))
		{
			return false;
		}
		for (std::uint32_t I = 0; I < ExtraCount; ++I)
		{
			std::uint32_t Key = 0;
			std::uint32_t Size = 0;
			if (!R.ReadU32(Key) || !R.ReadU32(Size) || !R.Skip(Size))
			{
				return false;
			}
		}
	}
	return true;
}

[[nodiscard]] bool BuildUncompressedDocument(
	const UPackage& Package,
	const std::unordered_map<std::string, UObject*>& Objects,
	std::vector<std::uint8_t>& OutDoc)
{
	OutDoc.clear();

	std::string DefaultObjectName;
	std::unordered_map<std::string, std::string> DependencyNameToFile;
	FByteWriter ObjectsPayload;
	std::uint32_t ObjectCount = 0;

	for (const auto& Pair : Objects)
	{
		UObject* Object = Pair.second;
		if (!Object)
		{
			continue;
		}
		if (DefaultObjectName.empty() && dynamic_cast<UResource*>(Object))
		{
			DefaultObjectName = Object->GetName();
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
			if (!OtherPackage || OtherPackage == &Package || !OtherPackage->IsPersistent())
			{
				continue;
			}
			DependencyNameToFile[OtherPackage->GetName()] = OtherPackage->GetFilePath();
		}

		if (!WriteObjectRecord(*Object, ObjectsPayload))
		{
			MAHO_CORE_ERROR("casset: failed encoding object '{}'", Object->GetName());
			return false;
		}
		++ObjectCount;
	}

	FByteWriter Pkg1;
	Pkg1.WriteString(Package.GetName());
	Pkg1.WriteU32(static_cast<std::uint32_t>(Package.GetPackageFlags()));
	Pkg1.WriteString(DefaultObjectName);
	Pkg1.WriteU32(ObjectCount);
	Pkg1.WriteU32(0);
	Pkg1.WriteU32(0);
	Pkg1.WriteU32(0);
	Pkg1.WriteU32(0);

	FByteWriter Deps;
	Deps.WriteU32(static_cast<std::uint32_t>(DependencyNameToFile.size()));
	for (const auto& Dep : DependencyNameToFile)
	{
		Deps.WriteString(Dep.first);
		Deps.WriteString(Dep.second);
		Deps.WriteU32(0);
	}

	FByteWriter Objs;
	Objs.WriteU32(ObjectCount);
	Objs.WriteBytes(ObjectsPayload.Bytes.data(), ObjectsPayload.Bytes.size());

	FByteWriter Doc;
	AppendChunk(Doc, kChunkTagPKG1, kChunkMustUnderstand, Pkg1.Bytes);
	AppendChunk(Doc, kChunkTagDEPS, kChunkMustUnderstand, Deps.Bytes);
	AppendChunk(Doc, kChunkTagOBJS, kChunkMustUnderstand, Objs.Bytes);
	OutDoc = std::move(Doc.Bytes);
	return true;
}

[[nodiscard]] bool ParseUncompressedDocument(const std::uint8_t* Data, std::size_t Size, FCassetParsedPackage& Out)
{
	Out = {};
	FByteReader R{Data, Size, 0};
	bool bHavePkg = false;
	bool bHaveDeps = false;
	bool bHaveObjs = false;

	while (R.Pos < R.Size)
	{
		if (!R.Remaining(12))
		{
			break;
		}
		std::uint32_t Tag = 0;
		std::uint32_t PayloadSize = 0;
		std::uint32_t ChunkFlags = 0;
		if (!R.ReadU32(Tag) || !R.ReadU32(PayloadSize) || !R.ReadU32(ChunkFlags))
		{
			return false;
		}
		if (!R.Remaining(PayloadSize))
		{
			MAHO_CORE_ERROR("casset: truncated chunk payload");
			return false;
		}
		FByteReader Payload{R.Data + R.Pos, PayloadSize, 0};
		R.Pos += PayloadSize;
		if (!R.SkipPadTo4())
		{
			return false;
		}

		if (Tag == kChunkTagPKG1)
		{
			std::uint32_t Hint = 0;
			std::uint32_t R0 = 0, R1 = 0, R2 = 0, R3 = 0;
			if (!Payload.ReadString(Out.Name) || !Payload.ReadU32(Out.PackageFlags)
				|| !Payload.ReadString(Out.DefaultObjectName) || !Payload.ReadU32(Hint)
				|| !Payload.ReadU32(R0) || !Payload.ReadU32(R1) || !Payload.ReadU32(R2)
				|| !Payload.ReadU32(R3))
			{
				return false;
			}
			bHavePkg = true;
		}
		else if (Tag == kChunkTagDEPS)
		{
			std::uint32_t Count = 0;
			if (!Payload.ReadU32(Count))
			{
				return false;
			}
			Out.Dependencies.resize(Count);
			for (std::uint32_t I = 0; I < Count; ++I)
			{
				if (!Payload.ReadString(Out.Dependencies[I].PackageName)
					|| !Payload.ReadString(Out.Dependencies[I].FilePath)
					|| !Payload.ReadU32(Out.Dependencies[I].Reserved))
				{
					return false;
				}
			}
			bHaveDeps = true;
		}
		else if (Tag == kChunkTagOBJS)
		{
			std::uint32_t Count = 0;
			if (!Payload.ReadU32(Count))
			{
				return false;
			}
			Out.Objects.resize(Count);
			for (std::uint32_t I = 0; I < Count; ++I)
			{
				if (!ParseObjectRecord(Payload, Out.Objects[I]))
				{
					MAHO_CORE_ERROR("casset: bad object record index {}", I);
					return false;
				}
			}
			bHaveObjs = true;
		}
		else if (ChunkFlags & kChunkMustUnderstand)
		{
			MAHO_CORE_ERROR("casset: unknown critical chunk tag=0x{:08X}", Tag);
			return false;
		}
	}

	if (!bHavePkg || !bHaveDeps || !bHaveObjs)
	{
		MAHO_CORE_ERROR("casset: missing required chunks PKG1/DEPS/OBJS");
		return false;
	}
	return true;
}

} // namespace

bool IsCassetBinaryFile(const std::uint8_t* Data, std::size_t Size)
{
	if (!Data || Size < 4)
	{
		return false;
	}
	const std::uint32_t Magic =
		static_cast<std::uint32_t>(Data[0])
		| (static_cast<std::uint32_t>(Data[1]) << 8)
		| (static_cast<std::uint32_t>(Data[2]) << 16)
		| (static_cast<std::uint32_t>(Data[3]) << 24);
	return Magic == kCassetMagic;
}

bool WriteCpuPayloadBytes(const UResource& Resource, std::vector<std::uint8_t>& OutCpuBytes)
{
	FByteWriter W;
	bool bOk = false;
	if (const UTexture* Tex = dynamic_cast<const UTexture*>(&Resource))
	{
		bOk = WriteTextureCpu(*Tex, W);
	}
	else if (const UStaticMesh* Mesh = dynamic_cast<const UStaticMesh*>(&Resource))
	{
		bOk = WriteMeshCpu(*Mesh, W);
	}
	else if (const UMaterial* Mat = dynamic_cast<const UMaterial*>(&Resource))
	{
		bOk = WriteMaterialCpu(*Mat, W);
	}
	else if (const USkeleton* Skeleton = dynamic_cast<const USkeleton*>(&Resource))
	{
		bOk = WriteSkeletonCpu(*Skeleton, W);
	}
	else if (const UAnimation* Anim = dynamic_cast<const UAnimation*>(&Resource))
	{
		bOk = WriteAnimationCpu(*Anim, W);
	}
	else if (const UAnimationGraph* Graph = dynamic_cast<const UAnimationGraph*>(&Resource))
	{
		bOk = WriteDocumentJsonCpu(Graph->GetDocumentJson(), W);
	}
	else if (const UPrefab* Prefab = dynamic_cast<const UPrefab*>(&Resource))
	{
		bOk = WriteDocumentJsonCpu(Prefab->GetDocumentJson(), W);
	}
	else
	{
		bOk = true;
	}
	if (bOk)
	{
		OutCpuBytes = std::move(W.Bytes);
	}
	return bOk;
}

bool ApplyCpuPayload(UResource& Resource, const std::vector<std::uint8_t>& CpuBytes)
{
	FByteReader R{CpuBytes.data(), CpuBytes.size(), 0};
	bool bOk = false;
	if (UTexture* Tex = dynamic_cast<UTexture*>(&Resource))
	{
		bOk = ReadTextureCpu(*Tex, R);
	}
	else if (UStaticMesh* Mesh = dynamic_cast<UStaticMesh*>(&Resource))
	{
		bOk = ReadMeshCpu(*Mesh, R);
	}
	else if (UMaterial* Mat = dynamic_cast<UMaterial*>(&Resource))
	{
		bOk = ReadMaterialCpu(*Mat, R);
	}
	else if (USkeleton* Skeleton = dynamic_cast<USkeleton*>(&Resource))
	{
		bOk = ReadSkeletonCpu(*Skeleton, R);
	}
	else if (UAnimation* Anim = dynamic_cast<UAnimation*>(&Resource))
	{
		bOk = ReadAnimationCpu(*Anim, R);
	}
	else if (UAnimationGraph* Graph = dynamic_cast<UAnimationGraph*>(&Resource))
	{
		std::string Json;
		bOk = ReadDocumentJsonCpu(Json, R);
		if (bOk)
		{
			Graph->SetDocumentJson(std::move(Json));
		}
	}
	else if (UPrefab* Prefab = dynamic_cast<UPrefab*>(&Resource))
	{
		std::string Json;
		bOk = ReadDocumentJsonCpu(Json, R);
		if (bOk)
		{
			Prefab->SetDocumentJson(std::move(Json));
		}
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

bool EncodePackageFile(const UPackage& Package, std::vector<std::uint8_t>& OutFileBytes)
{
	std::vector<std::uint8_t> Uncompressed;
	if (!BuildPackageDocument(Package, Uncompressed))
	{
		return false;
	}
	return WrapDocumentToMcasFile(Uncompressed, OutFileBytes);
}

bool BuildPackageDocument(const UPackage& Package, std::vector<std::uint8_t>& OutDocument)
{
	return BuildUncompressedDocument(Package, Package.Objects, OutDocument);
}

bool WrapDocumentToMcasFile(
	const std::vector<std::uint8_t>& UncompressedDocument,
	std::vector<std::uint8_t>& OutFileBytes)
{
	OutFileBytes.clear();
	std::vector<std::uint8_t> Compressed;
	if (!FCompression::CompressZlib(
			UncompressedDocument.data(),
			UncompressedDocument.size(),
			Compressed))
	{
		return false;
	}

	FByteWriter File;
	File.WriteU32(kCassetMagic);
	File.WriteU32(kCassetFormatVersion);
	File.WriteU32(kCassetHeaderSize);
	File.WriteU32(kCassetFlagBodyZlib);
	File.WriteU32(kCassetContentVersion);
	File.WriteU32(static_cast<std::uint32_t>(UncompressedDocument.size()));
	File.WriteU32(static_cast<std::uint32_t>(Compressed.size()));
	File.WriteU32(0); // Checksum (v1 unused)
	for (int I = 0; I < 8; ++I)
	{
		File.WriteU32(0);
	}

	File.WriteBytes(Compressed.data(), Compressed.size());
	OutFileBytes = std::move(File.Bytes);
	if (OutFileBytes.size() < kCassetHeaderSize)
	{
		MAHO_CORE_ERROR("casset: internal header size mismatch");
		return false;
	}
	return true;
}

bool DecodePackageFile(
	const std::uint8_t* FileBytes,
	std::size_t FileSize,
	FCassetParsedPackage& OutPackage)
{
	OutPackage = {};
	if (!IsCassetBinaryFile(FileBytes, FileSize) || FileSize < kCassetHeaderSize)
	{
		MAHO_CORE_ERROR("casset: not an MCAS binary package");
		return false;
	}

	FByteReader H{FileBytes, FileSize, 0};
	std::uint32_t Magic = 0, FormatVer = 0, HeaderSize = 0, Flags = 0, ContentVer = 0;
	std::uint32_t UncompSize = 0, CompSize = 0, Checksum = 0;
	if (!H.ReadU32(Magic) || !H.ReadU32(FormatVer) || !H.ReadU32(HeaderSize) || !H.ReadU32(Flags)
		|| !H.ReadU32(ContentVer) || !H.ReadU32(UncompSize) || !H.ReadU32(CompSize) || !H.ReadU32(Checksum))
	{
		return false;
	}
	(void)Checksum;
	if (Magic != kCassetMagic)
	{
		return false;
	}
	if (FormatVer > kCassetFormatVersion || ContentVer > kCassetContentVersion)
	{
		MAHO_CORE_ERROR(
			"casset: unsupported version format={} content={} (max {}/{})",
			FormatVer,
			ContentVer,
			kCassetFormatVersion,
			kCassetContentVersion);
		return false;
	}
	if (HeaderSize < 32 || HeaderSize > FileSize)
	{
		MAHO_CORE_ERROR("casset: bad HeaderSize {}", HeaderSize);
		return false;
	}
	H.Pos = HeaderSize;

	if ((Flags & kCassetFlagBodyZlib) == 0)
	{
		MAHO_CORE_ERROR("casset: uncompressed body not supported in v1");
		return false;
	}
	if (CompSize == 0 || UncompSize == 0 || !H.Remaining(CompSize))
	{
		MAHO_CORE_ERROR("casset: bad compressed body size");
		return false;
	}

	std::vector<std::uint8_t> Uncompressed;
	if (!FCompression::DecompressZlib(H.Data + H.Pos, CompSize, UncompSize, Uncompressed))
	{
		return false;
	}
	return ParseUncompressedDocument(Uncompressed.data(), Uncompressed.size(), OutPackage);
}

} // namespace ResourceCasset
} // namespace Maho
