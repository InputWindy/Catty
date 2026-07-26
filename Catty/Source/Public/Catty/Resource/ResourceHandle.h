#pragma once

#include "Catty/Core/Export.h"

#include <cstdint>
#include <string>

namespace Catty
{

/** Opaque resource identity issued by FResourceServer. */
struct FResourceId
{
	std::uint64_t Value = 0;

	[[nodiscard]] bool IsValid() const { return Value != 0; }

	friend bool operator==(FResourceId A, FResourceId B) { return A.Value == B.Value; }
	friend bool operator!=(FResourceId A, FResourceId B) { return A.Value != B.Value; }
};

enum class EResourceLoadState : std::uint8_t
{
	Invalid = 0,
	Pending,
	Ready,
	Failed
};

} // namespace Catty
