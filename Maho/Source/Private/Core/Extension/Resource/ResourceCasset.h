#pragma once

/**
 * Binary .casset (MCAS) package codec: FileHeader + zlib(Chunk document).
 * CPU payloads are raw blobs (no Base64 / JSON).
 */

#include <Core/Extension/Resource/Resource.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Maho
{

class UPackage;
class UResource;

namespace ResourceCasset
{

/** Disk magic 'M','C','A','S' as little-endian u32. */
inline constexpr std::uint32_t kCassetMagic =
	static_cast<std::uint32_t>('M')
	| (static_cast<std::uint32_t>('C') << 8)
	| (static_cast<std::uint32_t>('A') << 16)
	| (static_cast<std::uint32_t>('S') << 24);

inline constexpr std::uint32_t kCassetFormatVersion = 1;
inline constexpr std::uint32_t kCassetContentVersion = 1;
inline constexpr std::uint32_t kCassetHeaderSize = 64;
inline constexpr std::uint32_t kCassetFlagBodyZlib = 1u << 0;

inline constexpr std::uint32_t kChunkTagPKG1 =
	static_cast<std::uint32_t>('P')
	| (static_cast<std::uint32_t>('K') << 8)
	| (static_cast<std::uint32_t>('G') << 16)
	| (static_cast<std::uint32_t>('1') << 24);
inline constexpr std::uint32_t kChunkTagDEPS =
	static_cast<std::uint32_t>('D')
	| (static_cast<std::uint32_t>('E') << 8)
	| (static_cast<std::uint32_t>('P') << 16)
	| (static_cast<std::uint32_t>('S') << 24);
inline constexpr std::uint32_t kChunkTagOBJS =
	static_cast<std::uint32_t>('O')
	| (static_cast<std::uint32_t>('B') << 8)
	| (static_cast<std::uint32_t>('J') << 16)
	| (static_cast<std::uint32_t>('S') << 24);

inline constexpr std::uint32_t kChunkMustUnderstand = 1u << 0;

inline constexpr std::uint16_t kClassKindResource = 0;
inline constexpr std::uint16_t kClassKindObject = 1;

inline constexpr std::uint32_t kRecordHasCpu = 1u << 0;
inline constexpr std::uint32_t kRecordHasRefs = 1u << 1;
inline constexpr std::uint32_t kRecordHasExtras = 1u << 2;

inline constexpr std::uint16_t kCpuLayoutVersion = 1;
/** Texture CPU layout: 1 = raw pixels; 2 = optional encoded file blob. */
inline constexpr std::uint16_t kTextureCpuLayoutRaw = 1;
inline constexpr std::uint16_t kTextureCpuLayoutEncoded = 2;
inline constexpr std::uint8_t kTexturePayloadRaw = 0;
inline constexpr std::uint8_t kTexturePayloadEncoded = 1;

struct FCassetDependency
{
	std::string PackageName;
	std::string FilePath;
	std::uint32_t Reserved = 0;
};

struct FCassetParsedObject
{
	std::string Name;
	EResourceType Type = EResourceType::Unknown;
	std::uint16_t ClassKind = kClassKindResource;
	std::string ImportSource;
	std::uint32_t RecordFlags = 0;
	std::vector<std::string> Refs;
	/** Raw CPU sub-payload (after Object fixed header); interpreted by ApplyCpuPayload. */
	std::vector<std::uint8_t> CpuBytes;
};

struct FCassetParsedPackage
{
	std::string Name;
	std::uint32_t PackageFlags = 0;
	std::string DefaultObjectName;
	std::vector<FCassetDependency> Dependencies;
	std::vector<FCassetParsedObject> Objects;
};

[[nodiscard]] bool IsCassetBinaryFile(const std::uint8_t* Data, std::size_t Size);

/** Build zlib-wrapped MCAS file bytes from a live package (friend path via FResourceSystem). */
[[nodiscard]] bool EncodePackageFile(const UPackage& Package, std::vector<std::uint8_t>& OutFileBytes);

/**
 * Game-thread: snapshot package → uncompressed BinaryDocument (no zlib).
 * Worker-safe follow-up: WrapDocumentToMcasFile.
 */
[[nodiscard]] bool BuildPackageDocument(const UPackage& Package, std::vector<std::uint8_t>& OutDocument);

/** Any-thread: zlib + MCAS header around an uncompressed document. */
[[nodiscard]] bool WrapDocumentToMcasFile(
	const std::vector<std::uint8_t>& UncompressedDocument,
	std::vector<std::uint8_t>& OutFileBytes);

/** Inflate + parse MCAS bytes into FCassetParsedPackage. */
[[nodiscard]] bool DecodePackageFile(
	const std::uint8_t* FileBytes,
	std::size_t FileSize,
	FCassetParsedPackage& OutPackage);

/** Apply typed CPU blob onto a live resource (marks Ready / clears Dirty on success). */
[[nodiscard]] bool ApplyCpuPayload(UResource& Resource, const std::vector<std::uint8_t>& CpuBytes);

/** Write typed CPU blob for a resource. */
[[nodiscard]] bool WriteCpuPayloadBytes(const UResource& Resource, std::vector<std::uint8_t>& OutCpuBytes);

} // namespace ResourceCasset
} // namespace Maho
