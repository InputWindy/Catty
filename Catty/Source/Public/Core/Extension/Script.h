#pragma once

#include <Core/Delegate.h>
#include <Core/DependsPack.h>
#include <Core/Export.h>
#include <Core/Sequencer/EngineExtension.h>
#include <Core/TypeList.h>

#include <memory>
#include <string>
#include <vector>

namespace Catty
{

class FResourceManager;
class FScript;

/**
 * Types that can register themselves into the Lua VM.
 * FScript::Bind forwards to BindLua — no per-type hardcode on FScript.
 * Prefer auto-bind by listening to FScript::GetOnLuaReady().
 */
class CATTY_API ILuaBindable
{
public:
	virtual ~ILuaBindable() = default;

	/** Called when Lua is ready (or immediately if already initialized). */
	virtual void BindLua(FScript& Script) = 0;
};

/**
 * Lua VM extension (sol2 + Lua 5.4). Init after Resource so BindLua / reflect see a live catalog.
 * Runs on the game thread only — do not Call from worker / render threads.
 *
 * Built-in bindings (table `catty`):
 *   catty.log / log_warn / log_error(msg)
 *   catty.get/set_cvar_*
 *   catty.object / package / resource — codegen usertypes (snake_case methods)
 *
 * Extra bindings: implement ILuaBindable::BindLua and either
 *   Script.Bind(Obj) or subscribe to GetOnLuaReady() for auto-bind.
 * Per-frame OnUpdate calls stay on FScriptLayer.
 */
class CATTY_API FScript final
	: public IEngineExtension
	, public TDependsPack<
		TDependsOn<EEngineStage::Init, TTypeList<FResourceManager>>>
{
public:
	/** Fired after Lua Initialize succeeds (and after any Bind queued before init). */
	CATTY_DECLARE_MULTICAST_DELEGATE_OneParam(FOnLuaReady, FScript&);

	FScript();
	~FScript() override;

	FScript(const FScript&) = delete;
	FScript& operator=(const FScript&) = delete;

	[[nodiscard]] bool IsLuaInitialized() const { return bLuaInitialized; }
	[[nodiscard]] const std::string& GetScriptsDirectory() const { return ScriptsDirectory; }

	/**
	 * Opaque pointer to the engine sol::state (cast in .cpp that includes sol).
	 * nullptr if not initialized. Public headers must not depend on sol2.
	 */
	[[nodiscard]] void* TryGetLuaState();

	[[nodiscard]] FOnLuaReady& GetOnLuaReady() { return OnLuaReady; }
	[[nodiscard]] const FOnLuaReady& GetOnLuaReady() const { return OnLuaReady; }

	/**
	 * Forward to Bindable.BindLua(*this). If Lua is not ready yet, queues until Init.
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
	const char* GetName() const override { return "Script"; }
	bool ExecuteStage(EEngineStage Stage) override;

	[[nodiscard]] bool InitializeLua(const std::string& ScriptsDirectory);
	void ShutdownLua();

	struct FImpl;
	std::unique_ptr<FImpl> Impl;
	bool bLuaInitialized = false;
	std::string ScriptsDirectory;
	FOnLuaReady OnLuaReady;
	std::vector<ILuaBindable*> PendingBindables;
};

} // namespace Catty
