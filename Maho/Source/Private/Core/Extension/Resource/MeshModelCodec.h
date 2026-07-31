#pragma once

/**
 * Private CPU model codec for ResourceIO (Assimp when MAHO_WITH_ASSIMP).
 * Decode → FDecodedModelScene → Apply* to U* + UPrefab. Never touches RHI.
 */

#include <Core/Extension/Resource/Resource.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Maho
{

class FGCSystem;
class FResourceSystem;
class UPackage;

struct FDecodedCoordinateSystem
{
	EModelAxis Up = EModelAxis::Y;
	EModelAxis Forward = EModelAxis::Z;
	EModelHandedness Handedness = EModelHandedness::Right;
};

struct FDecodedModelMetadata
{
	FDecodedCoordinateSystem CoordinateSystem;
	float UnitScale = 1.f;
	std::unordered_map<std::string, std::string> ExtraKeys;
};

struct FDecodedBoneWeight
{
	std::uint32_t BoneIndex = 0;
	float Weight = 0.f;
};

struct FDecodedMesh
{
	std::string Name;
	std::vector<float> Positions;
	std::vector<float> Normals;
	std::vector<float> UVs;
	std::vector<std::uint32_t> Indices;
	/** Parallel to Positions/3 — optional skinning (Phase 1 may be empty). */
	std::vector<std::vector<FDecodedBoneWeight>> BoneWeights;
	std::int32_t MaterialIndex = -1;
};

struct FDecodedBone
{
	std::string Name;
	std::int32_t ParentIndex = -1;
	float BindLocal[16] = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1};
};

struct FDecodedSkeleton
{
	std::vector<FDecodedBone> Bones;
	[[nodiscard]] bool IsEmpty() const { return Bones.empty(); }
};

struct FDecodedAnimKey
{
	float Time = 0.f;
	float Translation[3] = {0, 0, 0};
	float Rotation[4] = {0, 0, 0, 1};
	float Scale[3] = {1, 1, 1};
};

struct FDecodedAnimTrack
{
	std::string TargetBoneName;
	std::vector<FDecodedAnimKey> Keys;
};

struct FDecodedAnimation
{
	std::string Name;
	float DurationSeconds = 0.f;
	std::vector<FDecodedAnimTrack> Tracks;
};

struct FDecodedTextureRef
{
	std::string SlotName;
	std::string SourcePath;
	std::vector<std::uint8_t> EmbeddedBytes;
};

struct FDecodedMaterial
{
	std::string Name;
	float BaseColorFactor[4] = {1.f, 1.f, 1.f, 1.f};
	float MetallicFactor = 0.f;
	float RoughnessFactor = 1.f;
	std::vector<FDecodedTextureRef> Textures;
};

struct FDecodedSceneNode
{
	std::string Name;
	std::int32_t ParentIndex = -1;
	float LocalTransform[16] = {
		1, 0, 0, 0,
		0, 1, 0, 0,
		0, 0, 1, 0,
		0, 0, 0, 1};
	std::int32_t MeshIndex = -1;
};

/** Temporary CPU bag from Decode — not a UObject, never saved. */
struct FDecodedModelScene
{
	std::string SourcePathHint;
	FDecodedModelMetadata Metadata;
	std::vector<FDecodedMesh> Meshes;
	FDecodedSkeleton Skeleton;
	std::vector<FDecodedAnimation> Animations;
	std::vector<FDecodedMaterial> Materials;
	std::vector<FDecodedSceneNode> Nodes;
};

namespace MeshModelCodec
{

[[nodiscard]] std::string GetExtensionLower(std::string_view Path);
[[nodiscard]] bool IsModelExtension(std::string_view Ext);
[[nodiscard]] bool MatchesModelSourcePath(std::string_view Path);

/**
 * Decode model bytes into FDecodedModelScene (Assimp when enabled).
 * Returns false if Assimp is unavailable or parse fails.
 */
[[nodiscard]] bool DecodeFromMemory(
	const std::uint8_t* Bytes,
	std::size_t ByteCount,
	std::string_view SourcePathHint,
	FDecodedModelScene& Out);

/**
 * Create sibling U* in Package from Decoded scene; fill Prefab document + SoftPaths.
 * Order: Materials(+textures stubs) → Meshes → Skeleton → Animations → Graph → Prefab JSON.
 */
[[nodiscard]] bool ApplyDecodedModelScene(
	FDecodedModelScene&& Scene,
	FResourceSystem& Resources,
	FGCSystem& GC,
	UPackage& Package,
	UPrefab& Prefab);

} // namespace MeshModelCodec
} // namespace Maho
