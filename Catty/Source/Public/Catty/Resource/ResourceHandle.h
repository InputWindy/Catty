#pragma once

#include "Catty/Core/Export.h"
#include "Catty/Core/ObjectReflect.h"

#include <cstdint>
#include <string>

namespace Catty
{

/** Opaque resource identity for async raw loads (issued by FResourceManager).
 *
 * Example:
 * ```
 *   Catty::FObjectRef Hero = ResourceManager.CreateResource(
 *       Pkg, "T_Hero", "Textures/T_Hero.png");
 *   ResourceManager.Flush(Hero);
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
