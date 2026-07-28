#pragma once

#include "Core/Delegate.h"
#include "Core/Export.h"

#include <memory>
#include <string>
#include <vector>

namespace Catty
{

class FScriptSystem;

/**
 * Types that can register themselves into the Lua VM.
 * FScriptSystem::Bind forwards to BindLua — no per-type hardcode on ScriptSystem.
 * Prefer auto-bind by listening to FScriptSystem::GetOnLuaReady().
 */
class CATTY_API ILuaBindable
{
public:
	virtual ~ILuaBindable() = default;

	/** Called when Lua is ready (or immediately if already initialized). */
	virtual void BindLua(FScriptSystem& Script) = 0;
};

/**
 * Embedded Lua VM (sol2 + Lua 5.4) for game logic scripting.
 * Runs on the game thread only — do not Call from worker / render threads.
 *
 * Built-in bindings (table `catty`):
 *   catty.log / log_warn / log_error(msg)
 *   catty.get/set_cvar_*
 *   catty.object / package / resource — codegen usertypes (snake_case methods)
 *
 * Extra bindings: implement ILuaBindable::BindLua and either
 *   Script.Bind(Obj) or subscribe to GetOnLuaReady() for auto-bind.
 */
class CATTY_API FScriptSystem
{
public:
	/** Fired after Initialize succeeds (and after any Bind queued before init). */
	CATTY_DECLARE_MULTICAST_DELEGATE_OneParam(FOnLuaReady, FScriptSystem&);

	FScriptSystem();
	~FScriptSystem();

	FScriptSystem(const FScriptSystem&) = delete;
	FScriptSystem& operator=(const FScriptSystem&) = delete;

	/**
	 * Create the Lua state, register core + object-reflect bindings, set package.path.
	 * Broadcasts OnLuaReady when done.
	 */
	[[nodiscard]] bool Initialize(const std::string& ScriptsDirectory = "Scripts");
	void Shutdown();

	[[nodiscard]] bool IsInitialized() const { return bInitialized; }
	[[nodiscard]] const std::string& GetScriptsDirectory() const { return ScriptsDirectory; }

	/**
	 * Opaque pointer to the engine sol::state (cast in .cpp that includes sol).
	 * nullptr if not initialized. Public headers must not depend on sol2.
	 */
	[[nodiscard]] void* TryGetLuaState();

	[[nodiscard]] FOnLuaReady& GetOnLuaReady() { return OnLuaReady; }
	[[nodiscard]] const FOnLuaReady& GetOnLuaReady() const { return OnLuaReady; }

	/**
	 * Forward to Bindable.BindLua(*this). If Lua is not ready yet, queues until Initialize.
	 */
	void Bind(ILuaBindable& Bindable);

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
	FOnLuaReady OnLuaReady;
	std::vector<ILuaBindable*> PendingBindables;
};

} // namespace Catty
