#pragma once

#include "Catty/Core/ConsoleVariable.h"
#include "Catty/Core/Export.h"

#include <string>
#include <vector>

namespace Catty
{

/**
 * Process-wide CVar registry (UE IConsoleManager subset).
 * Register via TAutoConsoleVariable / Register* helpers; load overrides from DefaultEngine.ini.
 */
class CATTY_API FConsoleManager
{
public:
	static FConsoleManager& Get();

	FConsoleManager(const FConsoleManager&) = delete;
	FConsoleManager& operator=(const FConsoleManager&) = delete;

	[[nodiscard]] IConsoleVariable* RegisterBool(
		const char* Name,
		bool DefaultValue,
		const char* Help,
		EConsoleVariableFlags Flags = EConsoleVariableFlags::Default);

	[[nodiscard]] IConsoleVariable* RegisterInt(
		const char* Name,
		int DefaultValue,
		const char* Help,
		EConsoleVariableFlags Flags = EConsoleVariableFlags::Default);

	[[nodiscard]] IConsoleVariable* RegisterFloat(
		const char* Name,
		float DefaultValue,
		const char* Help,
		EConsoleVariableFlags Flags = EConsoleVariableFlags::Default);

	[[nodiscard]] IConsoleVariable* RegisterString(
		const char* Name,
		const char* DefaultValue,
		const char* Help,
		EConsoleVariableFlags Flags = EConsoleVariableFlags::Default);

	[[nodiscard]] IConsoleVariable* Find(const char* Name) const;
	[[nodiscard]] IConsoleVariable* Find(const std::string& Name) const { return Find(Name.c_str()); }

	/**
	 * Apply [ConsoleVariables] from an .ini (UE DefaultEngine.ini style).
	 * Unknown names are logged and skipped. Returns number of variables set.
	 */
	int LoadConsoleVariablesFromIni(const std::string& IniFilePath);

	/** Apply [ConsoleVariables] section already loaded into a config file. */
	int ApplyConsoleVariablesSection(const class FConfigFile& Config, const char* SectionName = "ConsoleVariables");

	[[nodiscard]] std::vector<std::string> GetNames() const;

private:
	FConsoleManager() = default;
};

/**
 * Static registration helper (define in a .cpp — one instance per CVar name).
 * Example:
 *   static TAutoConsoleVariableInt CVarFoo("catty.Foo", 1, "Help text");
 */
class CATTY_API TAutoConsoleVariableBool
{
public:
	TAutoConsoleVariableBool(
		const char* Name,
		bool DefaultValue,
		const char* Help,
		EConsoleVariableFlags Flags = EConsoleVariableFlags::Default);

	[[nodiscard]] bool GetValue() const;
	[[nodiscard]] IConsoleVariable& AsVariable() const { return *Variable; }

private:
	IConsoleVariable* Variable = nullptr;
};

class CATTY_API TAutoConsoleVariableInt
{
public:
	TAutoConsoleVariableInt(
		const char* Name,
		int DefaultValue,
		const char* Help,
		EConsoleVariableFlags Flags = EConsoleVariableFlags::Default);

	[[nodiscard]] int GetValue() const;
	[[nodiscard]] IConsoleVariable& AsVariable() const { return *Variable; }

private:
	IConsoleVariable* Variable = nullptr;
};

class CATTY_API TAutoConsoleVariableFloat
{
public:
	TAutoConsoleVariableFloat(
		const char* Name,
		float DefaultValue,
		const char* Help,
		EConsoleVariableFlags Flags = EConsoleVariableFlags::Default);

	[[nodiscard]] float GetValue() const;
	[[nodiscard]] IConsoleVariable& AsVariable() const { return *Variable; }

private:
	IConsoleVariable* Variable = nullptr;
};

class CATTY_API TAutoConsoleVariableString
{
public:
	TAutoConsoleVariableString(
		const char* Name,
		const char* DefaultValue,
		const char* Help,
		EConsoleVariableFlags Flags = EConsoleVariableFlags::Default);

	[[nodiscard]] std::string GetValue() const;
	[[nodiscard]] IConsoleVariable& AsVariable() const { return *Variable; }

private:
	IConsoleVariable* Variable = nullptr;
};

/** Sync built-in catty.* CVars into FEngineConfig (call after loading ini). */
CATTY_API void ApplyEngineCVarsToConfig(struct FEngineConfig& OutConfig);

} // namespace Catty
