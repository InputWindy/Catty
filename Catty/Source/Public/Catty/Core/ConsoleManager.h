#pragma once

#include "Catty/Core/ConsoleVariable.h"
#include "Catty/Core/Export.h"

#include <string>
#include <vector>

namespace Catty
{

/**
 * Process-wide CVar registry (UE IConsoleManager subset).
 *
 * Cross-module string access (no need to include the registering TU's header):
 *   FConsoleManager::Get().GetInt("catty.Window.Width");
 *   if (IConsoleVariable* V = FConsoleManager::Get().Find("r.Foo")) { ... }
 *
 * Startup: FApp loads Config/DefaultEngine.ini [ConsoleVariables] before InitializeEngine,
 * then ApplyEngineCVarsToConfig. Unknown names are queued (early set) and applied on Register.
 *
 * Example:
 * ```
 *   // Register once in a .cpp (defining module):
 *   static Catty::TAutoConsoleVariableInt CVarQuality("mygame.Quality", 2, "0=low 2=high");
 *
 *   // Read / write from any module by name only:
 *   const int Q = Catty::FConsoleManager::Get().GetInt("mygame.Quality", 2);
 *   Catty::FConsoleManager::Get().SetInt("mygame.Quality", 3);
 *   Catty::FConsoleManager::Get().LoadConsoleVariablesFromIni("Config/DefaultEngine.ini");
 * ```
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

	/** Find by name (case-insensitive). nullptr if not registered yet (check early-set queue separately). */
	[[nodiscard]] IConsoleVariable* Find(const char* Name) const;
	[[nodiscard]] IConsoleVariable* Find(const std::string& Name) const { return Find(Name.c_str()); }

	/** Typed getters by name — safe across DLL/EXE without the defining header. */
	[[nodiscard]] bool GetBool(const char* Name, bool DefaultValue = false) const;
	[[nodiscard]] int GetInt(const char* Name, int DefaultValue = 0) const;
	[[nodiscard]] float GetFloat(const char* Name, float DefaultValue = 0.0f) const;
	[[nodiscard]] std::string GetString(const char* Name, const char* DefaultValue = "") const;

	[[nodiscard]] bool TryGetBool(const char* Name, bool& OutValue) const;
	[[nodiscard]] bool TryGetInt(const char* Name, int& OutValue) const;
	[[nodiscard]] bool TryGetFloat(const char* Name, float& OutValue) const;
	[[nodiscard]] bool TryGetString(const char* Name, std::string& OutValue) const;

	/**
	 * Set by name. If the CVar is not registered yet, queues an early set (UE-style)
	 * that is applied when Register* runs.
	 */
	bool SetBool(const char* Name, bool Value, EConsoleVariableSetBy SetBy = EConsoleVariableSetBy::Code);
	bool SetInt(const char* Name, int Value, EConsoleVariableSetBy SetBy = EConsoleVariableSetBy::Code);
	bool SetFloat(const char* Name, float Value, EConsoleVariableSetBy SetBy = EConsoleVariableSetBy::Code);
	bool SetString(const char* Name, const char* Value, EConsoleVariableSetBy SetBy = EConsoleVariableSetBy::Code);
	bool SetFromString(const char* Name, const char* Value, EConsoleVariableSetBy SetBy = EConsoleVariableSetBy::Code);

	/**
	 * Apply [ConsoleVariables] from an .ini (UE DefaultEngine.ini style).
	 * Returns number of variables applied or queued; -1 if the file cannot be opened.
	 */
	int LoadConsoleVariablesFromIni(const std::string& IniFilePath);

	/** Apply [ConsoleVariables] section already loaded into a config file. */
	int ApplyConsoleVariablesSection(
		const class FConfigFile& Config,
		const char* SectionName = "ConsoleVariables",
		EConsoleVariableSetBy SetBy = EConsoleVariableSetBy::ConsoleVariablesIni);

	[[nodiscard]] std::vector<std::string> GetNames() const;

	/** Log all registered CVars (name = value). */
	void Dump() const;

private:
	FConsoleManager() = default;
};

/**
 * Static registration helper (define in a .cpp — one instance per CVar name).
 * Other modules should read via FConsoleManager::Get().GetInt("name"), not this object.
 *
 * Example:
 * ```
 *   // MyModuleCVars.cpp
 *   static Catty::TAutoConsoleVariableInt CVarSamples(
 *       "r.MyPass.Samples", 4, "Sample count for MyPass");
 *
 *   void FMyPass::Execute()
 *   {
 *       const int Samples = CVarSamples.GetValue();
 *       // or: FConsoleManager::Get().GetInt("r.MyPass.Samples");
 *   }
 * ```
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
