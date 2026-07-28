#pragma once

#include "Catty/Core/Delegate.h"
#include "Catty/Core/Export.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Catty
{

class FApp;

enum class EModuleStage : std::uint8_t
{
	PreInit,
	Init,
	PostInit,

	BeginFrame,
	ProcessInput,
	FixedUpdate,
	Update,
	LateUpdate,
	PreRender,
	Render,
	PostRender,
	EndFrame,

	PreShutdown,
	Shutdown,
	PostShutdown,
};

/** Per-stage parameters. Global services go through FApp&. */
struct FStageContext
{
	float DeltaSeconds = 0.0f;
	float FixedDeltaSeconds = 0.0f;
	std::uint64_t FrameIndex = 0;
};

/**
 * Engine / plugin extension of a fixed pipeline stage.
 * Game content uses FLayer bound to PostStageDelegates instead.
 *
 * Lifecycle multicasts (Attach / Detach) mirror FLayer — listeners optional.
 * OnExitRequested: Broadcast to ask the app to quit; FApp binds on Attach.
 */
class CATTY_API IModule
{
public:
	using FOnAttach = TMulticastDelegate<void(IModule&)>;
	using FOnDetach = TMulticastDelegate<void(IModule&)>;
	using FOnExitRequested = TMulticastDelegate<void()>;

	virtual ~IModule()
	{
		Detach();
	}

	virtual const char* GetName() const = 0;

	/** Dependency module names (resolved after all RegisterModule calls). */
	virtual void GetDependencies(std::vector<std::string>& OutNames) const
	{
		(void)OutNames;
	}

	/**
	 * Fixed stage body (before Layers / Post broadcast).
	 * Init-family may return false to abort startup.
	 */
	virtual bool OnStage(EModuleStage Stage, FApp& App, FStageContext& Ctx)
	{
		(void)Stage;
		(void)App;
		(void)Ctx;
		return true;
	}

	[[nodiscard]] FOnExitRequested& GetOnExitRequested() { return OnExitRequested; }
	[[nodiscard]] const FOnExitRequested& GetOnExitRequested() const { return OnExitRequested; }

	/**
	 * Enter active lifetime: Broadcast GetOnAttach(), then virtual OnAttach().
	 * FApp calls after successful Init.
	 */
	void Attach()
	{
		if (bAttached)
		{
			return;
		}
		bAttached = true;
		AttachEvent.Broadcast(*this);
		OnAttach();
	}

	/**
	 * Leave active lifetime: Broadcast GetOnDetach(), then virtual OnDetach().
	 * FApp calls before Shutdown stages.
	 */
	void Detach()
	{
		if (!bAttached)
		{
			return;
		}
		bAttached = false;
		DetachEvent.Broadcast(*this);
		OnDetach();
	}

	[[nodiscard]] bool IsAttached() const { return bAttached; }

	[[nodiscard]] FOnAttach& GetOnAttach() { return AttachEvent; }
	[[nodiscard]] const FOnAttach& GetOnAttach() const { return AttachEvent; }
	[[nodiscard]] FOnDetach& GetOnDetach() { return DetachEvent; }
	[[nodiscard]] const FOnDetach& GetOnDetach() const { return DetachEvent; }

protected:
	/** Subclass hook after AttachEvent. */
	virtual void OnAttach() {}
	/** Subclass hook after DetachEvent. */
	virtual void OnDetach() {}

	FOnExitRequested OnExitRequested;

private:
	bool bAttached = false;
	FOnAttach AttachEvent;
	FOnDetach DetachEvent;
};

} // namespace Catty
