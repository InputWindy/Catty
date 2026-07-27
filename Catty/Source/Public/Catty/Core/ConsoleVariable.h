#pragma once

#include "Catty/Core/Export.h"

#include <cstdint>
#include <functional>
#include <string>

namespace Catty
{

enum class EConsoleVariableFlags : std::uint32_t
{
	Default = 0,
	/** Not intended for shipping cheats / debug toggles. */
	Cheat = 1u << 0,
	/** Ignore SetBy::Console (ini / code may still change). */
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

/**
 * Who last set the CVar (UE ECVF_SetBy* subset).
 * Higher value wins; lower-priority sources cannot override.
 */
enum class EConsoleVariableSetBy : std::uint32_t
{
	Constructor = 0x00000000,
	Scalability = 0x01000000,
	ProjectSetting = 0x03000000,
	SystemSettingsIni = 0x05000000,
	ConsoleVariablesIni = 0x06000000,
	Commandline = 0x07000000,
	Code = 0x08000000,
	Console = 0x09000000,
};

enum class EConsoleVariableType : std::uint8_t
{
	Bool = 0,
	Int,
	Float,
	String,
};

using FConsoleVariableChanged = std::function<void(class IConsoleVariable&)>;

/**
 * Runtime console variable (UE IConsoleVariable subset).
 * Owned by FConsoleManager. Cross-module access: FConsoleManager::Find / GetInt(name).
 *
 * Example:
 * ```
 *   if (Catty::IConsoleVariable* V = Catty::FConsoleManager::Get().Find("catty.Window.Width"))
 *   {
 *       CATTY_INFO("width={} setBy={}", V->GetInt(), static_cast<uint32_t>(V->GetSetBy()));
 *       V->SetOnChangedCallback([](Catty::IConsoleVariable& CVar)
 *       {
 *           CATTY_INFO("{} changed -> {}", CVar.GetName(), CVar.GetString());
 *       });
 *   }
 * ```
 */
class CATTY_API IConsoleVariable
{
public:
	virtual ~IConsoleVariable() = default;

	[[nodiscard]] virtual const std::string& GetName() const = 0;
	[[nodiscard]] virtual const std::string& GetHelp() const = 0;
	[[nodiscard]] virtual EConsoleVariableFlags GetFlags() const = 0;
	[[nodiscard]] virtual EConsoleVariableType GetType() const = 0;
	[[nodiscard]] virtual EConsoleVariableSetBy GetSetBy() const = 0;

	[[nodiscard]] virtual bool GetBool() const = 0;
	[[nodiscard]] virtual int GetInt() const = 0;
	[[nodiscard]] virtual float GetFloat() const = 0;
	[[nodiscard]] virtual std::string GetString() const = 0;

	/**
	 * Typed setters. Returns false if rejected (ReadOnly+Console, or lower SetBy priority).
	 * Default SetBy is Code (typical runtime / module call).
	 */
	virtual bool Set(bool Value, EConsoleVariableSetBy SetBy = EConsoleVariableSetBy::Code) = 0;
	virtual bool Set(int Value, EConsoleVariableSetBy SetBy = EConsoleVariableSetBy::Code) = 0;
	virtual bool Set(float Value, EConsoleVariableSetBy SetBy = EConsoleVariableSetBy::Code) = 0;
	virtual bool Set(const std::string& Value, EConsoleVariableSetBy SetBy = EConsoleVariableSetBy::Code) = 0;

	/** Parse a text token (ini / console / command line). */
	[[nodiscard]] virtual bool SetFromString(
		const std::string& Text,
		EConsoleVariableSetBy SetBy = EConsoleVariableSetBy::Code) = 0;

	virtual void SetOnChangedCallback(FConsoleVariableChanged Callback) = 0;
};

} // namespace Catty
