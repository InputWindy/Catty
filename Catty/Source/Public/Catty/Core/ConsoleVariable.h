#pragma once

#include "Catty/Core/Export.h"

#include <cstdint>
#include <string>

namespace Catty
{

enum class EConsoleVariableFlags : std::uint32_t
{
	Default = 0,
	/** Not intended for shipping cheats / debug toggles. */
	Cheat = 1u << 0,
	/** Set from code / ini only; console writes ignored (future). */
	ReadOnly = 1u << 1,
};

[[nodiscard]] inline EConsoleVariableFlags operator|(EConsoleVariableFlags A, EConsoleVariableFlags B)
{
	return static_cast<EConsoleVariableFlags>(static_cast<std::uint32_t>(A) | static_cast<std::uint32_t>(B));
}

[[nodiscard]] inline EConsoleVariableFlags operator&(EConsoleVariableFlags A, EConsoleVariableFlags B)
{
	return static_cast<EConsoleVariableFlags>(static_cast<std::uint32_t>(A) & static_cast<std::uint32_t>(B));
}

enum class EConsoleVariableType : std::uint8_t
{
	Bool = 0,
	Int,
	Float,
	String,
};

/**
 * Runtime console variable (UE IConsoleVariable subset).
 * Owned by FConsoleManager.
 */
class CATTY_API IConsoleVariable
{
public:
	virtual ~IConsoleVariable() = default;

	[[nodiscard]] virtual const std::string& GetName() const = 0;
	[[nodiscard]] virtual const std::string& GetHelp() const = 0;
	[[nodiscard]] virtual EConsoleVariableFlags GetFlags() const = 0;
	[[nodiscard]] virtual EConsoleVariableType GetType() const = 0;

	[[nodiscard]] virtual bool GetBool() const = 0;
	[[nodiscard]] virtual int GetInt() const = 0;
	[[nodiscard]] virtual float GetFloat() const = 0;
	[[nodiscard]] virtual std::string GetString() const = 0;

	virtual void Set(bool Value) = 0;
	virtual void Set(int Value) = 0;
	virtual void Set(float Value) = 0;
	virtual void Set(const std::string& Value) = 0;

	/** Parse a text token (ini / console) into the typed value. */
	[[nodiscard]] virtual bool SetFromString(const std::string& Text) = 0;
};

} // namespace Catty
