#pragma once

#include "Catty/Core/Export.h"
#include "Catty/Input/InputKeys.h"
#include "Catty/Platform/PlatformWindow.h"

#include <array>

namespace Catty
{

/**
 * Per-frame input snapshot.
 * Call Update() from FApp::ProcessInput after PollEvents.
 */
class CATTY_API FInput
{
public:
	void Update(const FPlatformWindow& Window);

	[[nodiscard]] bool IsKeyDown(EKey Key) const;
	[[nodiscard]] bool WasKeyPressed(EKey Key) const;
	[[nodiscard]] bool WasKeyReleased(EKey Key) const;

	[[nodiscard]] bool IsMouseButtonDown(EMouseButton Button) const;
	[[nodiscard]] bool WasMouseButtonPressed(EMouseButton Button) const;

	[[nodiscard]] float GetMouseX() const { return MouseX; }
	[[nodiscard]] float GetMouseY() const { return MouseY; }

private:
	static constexpr std::size_t KeyCount = static_cast<std::size_t>(EKey::Count);
	static constexpr std::size_t MouseCount = static_cast<std::size_t>(EMouseButton::Count);

	std::array<bool, KeyCount> Keys{};
	std::array<bool, KeyCount> PrevKeys{};
	std::array<bool, MouseCount> MouseButtons{};
	std::array<bool, MouseCount> PrevMouseButtons{};
	float MouseX = 0.0f;
	float MouseY = 0.0f;
};

} // namespace Catty
