#include "Catty/Input/Input.h"

namespace Catty
{

void FInput::Update(const FPlatformWindow& Window)
{
	PrevKeys = Keys;
	PrevMouseButtons = MouseButtons;

	for (std::size_t Index = 0; Index < KeyCount; ++Index)
	{
		Keys[Index] = Window.IsKeyDown(static_cast<EKey>(Index));
	}

	for (std::size_t Index = 0; Index < MouseCount; ++Index)
	{
		MouseButtons[Index] = Window.IsMouseButtonDown(static_cast<EMouseButton>(Index));
	}

	Window.GetCursorPosition(MouseX, MouseY);
}

bool FInput::IsKeyDown(EKey Key) const
{
	const std::size_t Index = static_cast<std::size_t>(Key);
	return Index < KeyCount && Keys[Index];
}

bool FInput::WasKeyPressed(EKey Key) const
{
	const std::size_t Index = static_cast<std::size_t>(Key);
	return Index < KeyCount && Keys[Index] && !PrevKeys[Index];
}

bool FInput::WasKeyReleased(EKey Key) const
{
	const std::size_t Index = static_cast<std::size_t>(Key);
	return Index < KeyCount && !Keys[Index] && PrevKeys[Index];
}

bool FInput::IsMouseButtonDown(EMouseButton Button) const
{
	const std::size_t Index = static_cast<std::size_t>(Button);
	return Index < MouseCount && MouseButtons[Index];
}

bool FInput::WasMouseButtonPressed(EMouseButton Button) const
{
	const std::size_t Index = static_cast<std::size_t>(Button);
	return Index < MouseCount && MouseButtons[Index] && !PrevMouseButtons[Index];
}

} // namespace Catty
