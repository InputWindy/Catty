#pragma once

#include <Core/Export.h>
#include <Core/ObjectReflect.h>

#include <cstdint>
#include <string>

namespace Catty
{

/** Opaque resource identity for async raw loads (issued by ResourceManager server).
 *
 * Example:
 * ```
 *   Catty::FObjectRef Hero = Catty::FindResourceByPath("/Game/Textures/T_Hero.T_Hero");
 *   Catty::FlushResource(Hero);
 * ```
 */
CATTY_STRUCT()
struct FResourceId
{
	CATTY_GENERATED_STRUCT_BODY()

	CATTY_PROPERTY()
	std::uint64_t Value = 0;

	[[nodiscard]] bool IsValid() const { return Value != 0; }

	friend bool operator==(FResourceId A, FResourceId B) { return A.Value == B.Value; }
	friend bool operator!=(FResourceId A, FResourceId B) { return A.Value != B.Value; }
};

CATTY_ENUM()
enum class EResourceLoadState : std::uint8_t
{
	Invalid = 0,
	Pending,
	Ready,
	Failed
};

} // namespace Catty
