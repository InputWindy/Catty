#pragma once

/**
 * Self-contained .casset CPU payload helpers (JSON + base64 blobs).
 * Package Serialize writes cpu; LoadPackage hydrates via FResourceSystem::LoadInlineResourceFromJson.
 */

#include <Core/Extension/Resource/Resource.h>
#include <Core/Json.h>

namespace Maho
{
namespace ResourceCasset
{

/** Append type-specific CPU fields onto an objects[] entry (already has name/class/type). */
[[nodiscard]] bool WriteCpuPayload(const UResource& Resource, FJsonValue& InOutEntry);

/** Apply cpu payload from an objects[] entry onto a live typed resource. */
[[nodiscard]] bool ReadCpuPayload(UResource& Resource, const FJsonValue& Entry);

} // namespace ResourceCasset
} // namespace Maho
