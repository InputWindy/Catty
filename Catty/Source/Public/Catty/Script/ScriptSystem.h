#pragma once

#include "Catty/Core/Export.h"

#include <memory>
#include <string>

namespace Catty
{

/**
 * Embedded Lua VM (sol2 + Lua 5.4) for game logic scripting.
 * Runs on the game thread only — do not Call from worker / render threads.
 *
 * Example:
 * ```
 *   ScriptSystem.Initialize("Scripts");
 *   ScriptSystem.DoFile("main.lua");   // defines OnUpdate(dt)
 *   // each frame:
 *   ScriptSystem.Call("OnUpdate", DeltaSeconds);
 *   ScriptSystem.Shutdown();
 * ```
 *
 * Built-in bindings (table `catty`):
 *   catty.log / log_warn / log_error(msg)
 *   catty.get_cvar_int / float / bool / string(name [, default])
 *   catty.set_cvar_int / float / bool / string(name, value)
 *
 * Generated usertypes (same `CATTY_REFLECT_*` macros → Tools/reflect_codegen.py):
 *   catty.<TypeName> with reflected properties / functions (opt-out: CATTY_LUA_SKIP)
 */
class CATTY_API FScriptSystem
{
public:
	FScriptSystem();
	~FScriptSystem();

	FScriptSystem(const FScriptSystem&) = delete;
	FScriptSystem& operator=(const FScriptSystem&) = delete;

	/**
	 * Create the Lua state, register core bindings, set package.path for Scripts/.
	 * @param ScriptsDirectory Project Scripts/ root (relative to process CWD is fine).
	 */
	[[nodiscard]] bool Initialize(const std::string& ScriptsDirectory = "Scripts");
	void Shutdown();

	[[nodiscard]] bool IsInitialized() const { return bInitialized; }
	[[nodiscard]] const std::string& GetScriptsDirectory() const { return ScriptsDirectory; }

	/**
	 * Load and run a .lua file. Relative paths are resolved under ScriptsDirectory.
	 * Returns false on load/runtime error (logged); does not abort the engine.
	 */
	[[nodiscard]] bool DoFile(const std::string& FilePath);

	[[nodiscard]] bool HasFunction(const char* FunctionName);

	/** Call a global Lua function with no args. Missing function → false (no error). */
	[[nodiscard]] bool Call(const char* FunctionName);

	/** Call a global Lua function with one float (e.g. OnUpdate / OnFixedUpdate). */
	[[nodiscard]] bool Call(const char* FunctionName, float Arg0);

private:
	struct FImpl;
	std::unique_ptr<FImpl> Impl;
	bool bInitialized = false;
	std::string ScriptsDirectory;
};

} // namespace Catty
