#include "MeshModelCodec.h"

#include <Core/Extension/GC/GC.h>
#include <Core/Extension/Resource/Resource.h>
#include <Core/Object/Package.h>
#include <Core/Object/SoftObjectPath.h>
#include <Core/System/Log.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_map>

#if defined(MAHO_WITH_ASSIMP) && MAHO_WITH_ASSIMP
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#endif

namespace Maho
{
namespace MeshModelCodec
{
namespace
{

std::string SanitizeObjectName(std::string_view Raw, std::string_view Fallback)
{
	std::string Out;
	Out.reserve(Raw.size());
	for (const char Ch : Raw)
	{
		if (std::isalnum(static_cast<unsigned char>(Ch)) || Ch == '_' || Ch == '-')
		{
			Out.push_back(Ch);
		}
		else if (Ch == ' ' || Ch == '.' || Ch == '/' || Ch == '\\')
		{
			Out.push_back('_');
		}
	}
	if (Out.empty())
	{
		Out.assign(Fallback.begin(), Fallback.end());
	}
	return Out;
}

void CopyMat4(float Out[16], const float* In)
{
	for (int I = 0; I < 16; ++I)
	{
		Out[I] = In[I];
	}
}

template <typename TResource>
[[nodiscard]] TResource* CreateRegisteredResource(
	FResourceSystem& Resources,
	FGCSystem& GC,
	UPackage& Package,
	const std::string& Name,
	EResourceType Type,
	const std::string& SourcePath)
{
	if (Package.FindObject(Name))
	{
		MAHO_CORE_ERROR("MeshModelCodec: '{}' already exists in package '{}'", Name, Package.GetName());
		return nullptr;
	}

	FObjectRef Ref = GC.NewObject<TResource>(&Package, Name, Type, SourcePath);
	TResource* Obj = Ref.Cast<TResource>();
	if (!Obj)
	{
		MAHO_CORE_ERROR("MeshModelCodec: NewObject failed for '{}'", Name);
		return nullptr;
	}

	if (!Resources.RegisterOwnedResource(Package, Ref))
	{
		MAHO_CORE_ERROR("MeshModelCodec: RegisterOwnedResource failed for '{}'", Name);
		return nullptr;
	}

	return Obj;
}

#if defined(MAHO_WITH_ASSIMP) && MAHO_WITH_ASSIMP

void CopyAiMatrix(float Out[16], const aiMatrix4x4& M)
{
	Out[0] = M.a1;
	Out[1] = M.b1;
	Out[2] = M.c1;
	Out[3] = M.d1;
	Out[4] = M.a2;
	Out[5] = M.b2;
	Out[6] = M.c2;
	Out[7] = M.d2;
	Out[8] = M.a3;
	Out[9] = M.b3;
	Out[10] = M.c3;
	Out[11] = M.d3;
	Out[12] = M.a4;
	Out[13] = M.b4;
	Out[14] = M.c4;
	Out[15] = M.d4;
}

void CollectNodes(
	const aiNode* Node,
	std::int32_t ParentIndex,
	std::vector<FDecodedSceneNode>& OutNodes)
{
	if (!Node)
	{
		return;
	}

	const std::int32_t SelfIndex = static_cast<std::int32_t>(OutNodes.size());
	FDecodedSceneNode Decoded;
	Decoded.Name = Node->mName.C_Str();
	Decoded.ParentIndex = ParentIndex;
	CopyAiMatrix(Decoded.LocalTransform, Node->mTransformation);
	Decoded.MeshIndex = Node->mNumMeshes > 0 ? static_cast<std::int32_t>(Node->mMeshes[0]) : -1;
	OutNodes.push_back(std::move(Decoded));

	for (unsigned I = 0; I < Node->mNumChildren; ++I)
	{
		CollectNodes(Node->mChildren[I], SelfIndex, OutNodes);
	}
}

void DecodeMesh(const aiMesh* Mesh, FDecodedMesh& Out)
{
	Out.Name = Mesh->mName.C_Str();
	Out.MaterialIndex = static_cast<std::int32_t>(Mesh->mMaterialIndex);
	Out.Positions.reserve(Mesh->mNumVertices * 3);
	Out.Normals.reserve(Mesh->mNumVertices * 3);
	Out.UVs.reserve(Mesh->mNumVertices * 2);

	for (unsigned V = 0; V < Mesh->mNumVertices; ++V)
	{
		Out.Positions.push_back(Mesh->mVertices[V].x);
		Out.Positions.push_back(Mesh->mVertices[V].y);
		Out.Positions.push_back(Mesh->mVertices[V].z);
		if (Mesh->HasNormals())
		{
			Out.Normals.push_back(Mesh->mNormals[V].x);
			Out.Normals.push_back(Mesh->mNormals[V].y);
			Out.Normals.push_back(Mesh->mNormals[V].z);
		}
		if (Mesh->HasTextureCoords(0))
		{
			Out.UVs.push_back(Mesh->mTextureCoords[0][V].x);
			Out.UVs.push_back(Mesh->mTextureCoords[0][V].y);
		}
		else
		{
			Out.UVs.push_back(0.f);
			Out.UVs.push_back(0.f);
		}
	}

	Out.Indices.reserve(Mesh->mNumFaces * 3);
	for (unsigned F = 0; F < Mesh->mNumFaces; ++F)
	{
		const aiFace& Face = Mesh->mFaces[F];
		if (Face.mNumIndices != 3)
		{
			continue;
		}
		Out.Indices.push_back(Face.mIndices[0]);
		Out.Indices.push_back(Face.mIndices[1]);
		Out.Indices.push_back(Face.mIndices[2]);
	}

	if (Mesh->mNumBones > 0)
	{
		Out.BoneWeights.resize(Mesh->mNumVertices);
		for (unsigned B = 0; B < Mesh->mNumBones; ++B)
		{
			const aiBone* Bone = Mesh->mBones[B];
			for (unsigned W = 0; W < Bone->mNumWeights; ++W)
			{
				const aiVertexWeight& VW = Bone->mWeights[W];
				if (VW.mVertexId >= Mesh->mNumVertices)
				{
					continue;
				}
				FDecodedBoneWeight Entry;
				Entry.BoneIndex = B;
				Entry.Weight = VW.mWeight;
				Out.BoneWeights[VW.mVertexId].push_back(Entry);
			}
		}
	}
}

void DecodeMaterials(const aiScene* Scene, FDecodedModelScene& Out)
{
	Out.Materials.reserve(Scene->mNumMaterials);
	for (unsigned I = 0; I < Scene->mNumMaterials; ++I)
	{
		const aiMaterial* Mat = Scene->mMaterials[I];
		FDecodedMaterial Decoded;
		aiString Name;
		if (Mat->Get(AI_MATKEY_NAME, Name) == AI_SUCCESS)
		{
			Decoded.Name = Name.C_Str();
		}
		else
		{
			Decoded.Name = "Material_" + std::to_string(I);
		}

		aiColor4D Color;
		if (Mat->Get(AI_MATKEY_COLOR_DIFFUSE, Color) == AI_SUCCESS)
		{
			Decoded.BaseColorFactor[0] = Color.r;
			Decoded.BaseColorFactor[1] = Color.g;
			Decoded.BaseColorFactor[2] = Color.b;
			Decoded.BaseColorFactor[3] = Color.a;
		}

		aiString TexPath;
		if (Mat->GetTexture(aiTextureType_DIFFUSE, 0, &TexPath) == AI_SUCCESS)
		{
			FDecodedTextureRef Tex;
			Tex.SlotName = "BaseColor";
			Tex.SourcePath = TexPath.C_Str();
			Decoded.Textures.push_back(std::move(Tex));
		}

		Out.Materials.push_back(std::move(Decoded));
	}
}

void BuildSkeletonFromBones(const aiScene* Scene, FDecodedSkeleton& Out)
{
	std::unordered_map<std::string, std::int32_t> NameToIndex;
	for (unsigned M = 0; M < Scene->mNumMeshes; ++M)
	{
		const aiMesh* Mesh = Scene->mMeshes[M];
		for (unsigned B = 0; B < Mesh->mNumBones; ++B)
		{
			const aiBone* Bone = Mesh->mBones[B];
			const std::string Name = Bone->mName.C_Str();
			if (NameToIndex.count(Name) != 0)
			{
				continue;
			}
			FDecodedBone Decoded;
			Decoded.Name = Name;
			Decoded.ParentIndex = -1;
			CopyAiMatrix(Decoded.BindLocal, Bone->mOffsetMatrix);
			NameToIndex.emplace(Name, static_cast<std::int32_t>(Out.Bones.size()));
			Out.Bones.push_back(std::move(Decoded));
		}
	}
}

void DecodeAnimations(const aiScene* Scene, FDecodedModelScene& Out)
{
	Out.Animations.reserve(Scene->mNumAnimations);
	for (unsigned A = 0; A < Scene->mNumAnimations; ++A)
	{
		const aiAnimation* Anim = Scene->mAnimations[A];
		FDecodedAnimation Decoded;
		Decoded.Name = Anim->mName.length > 0 ? Anim->mName.C_Str() : ("Anim_" + std::to_string(A));
		const double Ticks = Anim->mTicksPerSecond > 0.0 ? Anim->mTicksPerSecond : 25.0;
		Decoded.DurationSeconds = static_cast<float>(Anim->mDuration / Ticks);

		Decoded.Tracks.reserve(Anim->mNumChannels);
		for (unsigned C = 0; C < Anim->mNumChannels; ++C)
		{
			const aiNodeAnim* Channel = Anim->mChannels[C];
			FDecodedAnimTrack Track;
			Track.TargetBoneName = Channel->mNodeName.C_Str();

			unsigned KeyCount = Channel->mNumPositionKeys;
			if (Channel->mNumRotationKeys > KeyCount)
			{
				KeyCount = Channel->mNumRotationKeys;
			}
			if (Channel->mNumScalingKeys > KeyCount)
			{
				KeyCount = Channel->mNumScalingKeys;
			}
			Track.Keys.reserve(KeyCount);
			for (unsigned K = 0; K < KeyCount; ++K)
			{
				FDecodedAnimKey Key;
				if (K < Channel->mNumPositionKeys)
				{
					Key.Time = static_cast<float>(Channel->mPositionKeys[K].mTime / Ticks);
					Key.Translation[0] = Channel->mPositionKeys[K].mValue.x;
					Key.Translation[1] = Channel->mPositionKeys[K].mValue.y;
					Key.Translation[2] = Channel->mPositionKeys[K].mValue.z;
				}
				if (K < Channel->mNumRotationKeys)
				{
					Key.Time = static_cast<float>(Channel->mRotationKeys[K].mTime / Ticks);
					Key.Rotation[0] = Channel->mRotationKeys[K].mValue.x;
					Key.Rotation[1] = Channel->mRotationKeys[K].mValue.y;
					Key.Rotation[2] = Channel->mRotationKeys[K].mValue.z;
					Key.Rotation[3] = Channel->mRotationKeys[K].mValue.w;
				}
				if (K < Channel->mNumScalingKeys)
				{
					Key.Time = static_cast<float>(Channel->mScalingKeys[K].mTime / Ticks);
					Key.Scale[0] = Channel->mScalingKeys[K].mValue.x;
					Key.Scale[1] = Channel->mScalingKeys[K].mValue.y;
					Key.Scale[2] = Channel->mScalingKeys[K].mValue.z;
				}
				Track.Keys.push_back(Key);
			}
			Decoded.Tracks.push_back(std::move(Track));
		}
		Out.Animations.push_back(std::move(Decoded));
	}
}

bool DecodeWithAssimp(
	const std::uint8_t* Bytes,
	std::size_t ByteCount,
	std::string_view SourcePathHint,
	FDecodedModelScene& Out)
{
	Assimp::Importer Importer;
	const unsigned Flags =
		aiProcess_Triangulate |
		aiProcess_GenSmoothNormals |
		aiProcess_JoinIdenticalVertices |
		aiProcess_ImproveCacheLocality |
		aiProcess_LimitBoneWeights;

	const std::string Hint(SourcePathHint);
	const aiScene* Scene = Importer.ReadFileFromMemory(
		Bytes,
		ByteCount,
		Flags,
		Hint.empty() ? nullptr : Hint.c_str());
	if (!Scene || (Scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !Scene->mRootNode)
	{
		MAHO_CORE_ERROR("MeshModelCodec: Assimp failed: {}", Importer.GetErrorString());
		return false;
	}

	Out = FDecodedModelScene{};
	Out.SourcePathHint = Hint;
	Out.Metadata.CoordinateSystem.Up = EModelAxis::Y;
	Out.Metadata.CoordinateSystem.Forward = EModelAxis::NegZ;
	Out.Metadata.CoordinateSystem.Handedness = EModelHandedness::Right;
	Out.Metadata.UnitScale = 1.f;

	DecodeMaterials(Scene, Out);

	Out.Meshes.reserve(Scene->mNumMeshes);
	for (unsigned I = 0; I < Scene->mNumMeshes; ++I)
	{
		FDecodedMesh Mesh;
		DecodeMesh(Scene->mMeshes[I], Mesh);
		if (Mesh.Name.empty())
		{
			Mesh.Name = "Mesh_" + std::to_string(I);
		}
		Out.Meshes.push_back(std::move(Mesh));
	}

	BuildSkeletonFromBones(Scene, Out.Skeleton);
	DecodeAnimations(Scene, Out);
	CollectNodes(Scene->mRootNode, -1, Out.Nodes);
	return true;
}

#endif // MAHO_WITH_ASSIMP

std::string AxisToString(EModelAxis Axis)
{
	switch (Axis)
	{
	case EModelAxis::X: return "X";
	case EModelAxis::Y: return "Y";
	case EModelAxis::Z: return "Z";
	case EModelAxis::NegX: return "-X";
	case EModelAxis::NegY: return "-Y";
	case EModelAxis::NegZ: return "-Z";
	default: return "Y";
	}
}

std::string EscapeJson(const std::string& Value)
{
	std::string Out;
	Out.reserve(Value.size() + 8);
	for (const char Ch : Value)
	{
		if (Ch == '\\' || Ch == '"')
		{
			Out.push_back('\\');
		}
		Out.push_back(Ch);
	}
	return Out;
}

std::string BuildAnimationGraphJson(
	const std::vector<std::string>& AnimSoftPaths,
	const std::string& DefaultSoftPath)
{
	std::ostringstream Oss;
	Oss << "{\n  \"Animations\": [";
	for (std::size_t I = 0; I < AnimSoftPaths.size(); ++I)
	{
		if (I != 0)
		{
			Oss << ", ";
		}
		Oss << "\"" << EscapeJson(AnimSoftPaths[I]) << "\"";
	}
	Oss << "],\n  \"DefaultAnimation\": \"" << EscapeJson(DefaultSoftPath) << "\"\n}\n";
	return Oss.str();
}

std::string BuildPrefabDocumentJson(
	const FDecodedModelMetadata& Meta,
	const std::vector<std::string>& MeshSoftPaths,
	const std::string& SkeletonSoftPath,
	const std::string& GraphSoftPath)
{
	std::ostringstream Oss;
	Oss << "{\n";
	Oss << "  \"Metadata\": {\n";
	Oss << "    \"CoordinateSystem\": {\n";
	Oss << "      \"Up\": \"" << AxisToString(Meta.CoordinateSystem.Up) << "\",\n";
	Oss << "      \"Forward\": \"" << AxisToString(Meta.CoordinateSystem.Forward) << "\",\n";
	Oss << "      \"Handedness\": \""
		<< (Meta.CoordinateSystem.Handedness == EModelHandedness::Left ? "Left" : "Right")
		<< "\"\n";
	Oss << "    },\n";
	Oss << "    \"UnitScale\": " << Meta.UnitScale << "\n";
	Oss << "  },\n";
	Oss << "  \"Meshes\": [";
	for (std::size_t I = 0; I < MeshSoftPaths.size(); ++I)
	{
		if (I != 0)
		{
			Oss << ", ";
		}
		Oss << "\"" << EscapeJson(MeshSoftPaths[I]) << "\"";
	}
	Oss << "],\n";
	Oss << "  \"Skeleton\": \"" << EscapeJson(SkeletonSoftPath) << "\",\n";
	Oss << "  \"AnimationGraph\": \"" << EscapeJson(GraphSoftPath) << "\"\n";
	Oss << "}\n";
	return Oss.str();
}

} // namespace

std::string GetExtensionLower(std::string_view Path)
{
	const auto Slash = Path.find_last_of("/\\");
	const std::string_view Name = Slash == std::string_view::npos ? Path : Path.substr(Slash + 1);
	const auto Dot = Name.find_last_of('.');
	if (Dot == std::string_view::npos || Dot + 1 >= Name.size())
	{
		return {};
	}
	std::string Ext(Name.substr(Dot + 1));
	std::transform(Ext.begin(), Ext.end(), Ext.begin(), [](unsigned char C) {
		return static_cast<char>(std::tolower(C));
	});
	return Ext;
}

bool IsModelExtension(std::string_view Ext)
{
	return Ext == "fbx" || Ext == "gltf" || Ext == "glb" || Ext == "obj" || Ext == "dae" ||
		Ext == "blend" || Ext == "3ds" || Ext == "ase" || Ext == "ply" || Ext == "stl";
}

bool MatchesModelSourcePath(std::string_view Path)
{
	return IsModelExtension(GetExtensionLower(Path));
}

bool DecodeFromMemory(
	const std::uint8_t* Bytes,
	std::size_t ByteCount,
	std::string_view SourcePathHint,
	FDecodedModelScene& Out)
{
	if (!Bytes || ByteCount == 0)
	{
		return false;
	}

#if defined(MAHO_WITH_ASSIMP) && MAHO_WITH_ASSIMP
	return DecodeWithAssimp(Bytes, ByteCount, SourcePathHint, Out);
#else
	(void)SourcePathHint;
	(void)Out;
	MAHO_CORE_ERROR("MeshModelCodec: Assimp disabled (MAHO_WITH_ASSIMP=0); cannot decode model");
	return false;
#endif
}

bool ApplyDecodedModelScene(
	FDecodedModelScene&& Scene,
	FResourceSystem& Resources,
	FGCSystem& GC,
	UPackage& Package,
	UPrefab& Prefab)
{
	std::vector<std::string> MeshSoftPathStrings;
	std::vector<UMaterial*> Materials;
	Materials.reserve(Scene.Materials.size());

	for (std::size_t I = 0; I < Scene.Materials.size(); ++I)
	{
		const FDecodedMaterial& Src = Scene.Materials[I];
		const std::string Name = SanitizeObjectName(Src.Name, "Material_" + std::to_string(I));
		UMaterial* Mat = CreateRegisteredResource<UMaterial>(
			Resources,
			GC,
			Package,
			Name,
			EResourceType::Material,
			{});
		if (!Mat)
		{
			return false;
		}
		Mat->BaseColorFactor[0] = Src.BaseColorFactor[0];
		Mat->BaseColorFactor[1] = Src.BaseColorFactor[1];
		Mat->BaseColorFactor[2] = Src.BaseColorFactor[2];
		Mat->BaseColorFactor[3] = Src.BaseColorFactor[3];
		Mat->MetallicFactor = Src.MetallicFactor;
		Mat->RoughnessFactor = Src.RoughnessFactor;
		for (const FDecodedTextureRef& Tex : Src.Textures)
		{
			if (!Tex.SourcePath.empty() && Tex.SlotName == "BaseColor")
			{
				Mat->SetBaseColorTexture(FSoftObjectPath(Tex.SourcePath));
			}
			else if (!Tex.SourcePath.empty() && Tex.SlotName == "Normal")
			{
				Mat->SetNormalTexture(FSoftObjectPath(Tex.SourcePath));
			}
			else if (!Tex.SourcePath.empty() && Tex.SlotName == "MetallicRoughness")
			{
				Mat->SetMetallicRoughnessTexture(FSoftObjectPath(Tex.SourcePath));
			}
		}
		Mat->MarkCpuReady();
		Materials.push_back(Mat);
	}

	for (std::size_t I = 0; I < Scene.Meshes.size(); ++I)
	{
		FDecodedMesh& Src = Scene.Meshes[I];
		const std::string Name = SanitizeObjectName(Src.Name, "Mesh_" + std::to_string(I));
		UStaticMesh* Mesh = CreateRegisteredResource<UStaticMesh>(
			Resources,
			GC,
			Package,
			Name,
			EResourceType::Mesh,
			{});
		if (!Mesh)
		{
			return false;
		}
		Mesh->SetCpuGeometry(
			std::move(Src.Positions),
			std::move(Src.Normals),
			std::move(Src.UVs),
			std::move(Src.Indices));
		if (Src.MaterialIndex >= 0 &&
			static_cast<std::size_t>(Src.MaterialIndex) < Materials.size() &&
			Materials[static_cast<std::size_t>(Src.MaterialIndex)])
		{
			Mesh->SetMaterial(
				FSoftObjectPath::FromObject(*Materials[static_cast<std::size_t>(Src.MaterialIndex)]));
		}
		Mesh->MarkCpuReady();
		MeshSoftPathStrings.push_back(FSoftObjectPath::FromObject(*Mesh).ToString());
	}

	std::string SkeletonSoftPath;
	FSoftObjectPath SkeletonPath;
	if (!Scene.Skeleton.IsEmpty())
	{
		USkeleton* Skeleton = CreateRegisteredResource<USkeleton>(
			Resources,
			GC,
			Package,
			"Skeleton",
			EResourceType::Skeleton,
			{});
		if (!Skeleton)
		{
			return false;
		}
		std::vector<FSkeletonBone> Bones;
		Bones.reserve(Scene.Skeleton.Bones.size());
		for (const FDecodedBone& Bone : Scene.Skeleton.Bones)
		{
			FSkeletonBone OutBone;
			OutBone.Name = Bone.Name;
			OutBone.ParentIndex = Bone.ParentIndex;
			CopyMat4(OutBone.BindLocal, Bone.BindLocal);
			Bones.push_back(std::move(OutBone));
		}
		Skeleton->SetBones(std::move(Bones));
		Skeleton->MarkCpuReady();
		SkeletonPath = FSoftObjectPath::FromObject(*Skeleton);
		SkeletonSoftPath = SkeletonPath.ToString();
	}

	std::vector<std::string> AnimSoftPathStrings;
	AnimSoftPathStrings.reserve(Scene.Animations.size());
	for (std::size_t I = 0; I < Scene.Animations.size(); ++I)
	{
		FDecodedAnimation& Src = Scene.Animations[I];
		const std::string Name = SanitizeObjectName(Src.Name, "Animation_" + std::to_string(I));
		UAnimation* Anim = CreateRegisteredResource<UAnimation>(
			Resources,
			GC,
			Package,
			Name,
			EResourceType::Animation,
			{});
		if (!Anim)
		{
			return false;
		}
		Anim->SetDurationSeconds(Src.DurationSeconds);
		if (SkeletonPath.IsValid())
		{
			Anim->SetSkeleton(SkeletonPath);
		}
		std::vector<FAnimationTrack> Tracks;
		Tracks.reserve(Src.Tracks.size());
		for (FDecodedAnimTrack& Track : Src.Tracks)
		{
			FAnimationTrack OutTrack;
			OutTrack.TargetBoneName = std::move(Track.TargetBoneName);
			OutTrack.Keys.reserve(Track.Keys.size());
			for (const FDecodedAnimKey& Key : Track.Keys)
			{
				FAnimationKey OutKey;
				OutKey.Time = Key.Time;
				OutKey.Translation[0] = Key.Translation[0];
				OutKey.Translation[1] = Key.Translation[1];
				OutKey.Translation[2] = Key.Translation[2];
				OutKey.Rotation[0] = Key.Rotation[0];
				OutKey.Rotation[1] = Key.Rotation[1];
				OutKey.Rotation[2] = Key.Rotation[2];
				OutKey.Rotation[3] = Key.Rotation[3];
				OutKey.Scale[0] = Key.Scale[0];
				OutKey.Scale[1] = Key.Scale[1];
				OutKey.Scale[2] = Key.Scale[2];
				OutTrack.Keys.push_back(OutKey);
			}
			Tracks.push_back(std::move(OutTrack));
		}
		Anim->SetTracks(std::move(Tracks));
		Anim->MarkCpuReady();
		AnimSoftPathStrings.push_back(FSoftObjectPath::FromObject(*Anim).ToString());
	}

	UAnimationGraph* Graph = CreateRegisteredResource<UAnimationGraph>(
		Resources,
		GC,
		Package,
		"AnimationGraph",
		EResourceType::AnimationGraph,
		{});
	if (!Graph)
	{
		return false;
	}
	const std::string DefaultAnim =
		AnimSoftPathStrings.empty() ? std::string{} : AnimSoftPathStrings.front();
	Graph->SetDocumentJson(BuildAnimationGraphJson(AnimSoftPathStrings, DefaultAnim));
	Graph->MarkCpuReady();
	const std::string GraphSoftPath = FSoftObjectPath::FromObject(*Graph).ToString();

	Prefab.SetDocumentJson(BuildPrefabDocumentJson(
		Scene.Metadata,
		MeshSoftPathStrings,
		SkeletonSoftPath,
		GraphSoftPath));
	return true;
}

} // namespace MeshModelCodec
} // namespace Maho
