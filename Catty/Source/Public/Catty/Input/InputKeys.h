#pragma once

#include "Catty/Core/Export.h"

#include <cstdint>

namespace Catty
{

/** Keyboard keys used by FInput / FPlatformWindow (independent of GLFW codes). */
enum class EKey : std::uint16_t
{
	Unknown = 0,
	Escape,
	Space,
	Enter,
	W,
	A,
	S,
	D,
	Q,
	E,
	Left,
	Right,
	Up,
	Down,
	Count
};

enum class EMouseButton : std::uint8_t
{
	Left = 0,
	Right,
	Middle,
	Count
};

} // namespace Catty
