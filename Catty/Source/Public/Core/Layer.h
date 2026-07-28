#pragma once

#include <Core/Delegate.h>
#include <Core/Export.h>
#include <Core/Module.h>

#include <string>

namespace Catty
{

class FApp;

/**
 * Game / editor slice bound to FApp PostStageDelegates on PushLayer / PushOverlay.
 * Lifecycle: Attach() / Detach() fire multicasts then virtual hooks.
 * FApp listens GetOnDetach() to auto-remove Post bindings.
 *
 * Example:
 * ```
 *   class FWorldLayer : public Catty::FLayer
 *   {
 *   public:
 *       FWorldLayer() : Catty::FLayer("WorldLayer") {}
 *       virtual void OnUpdate(EModuleStage, FApp&, FStageContext& Ctx) override
 *       {
 *           TickWorld(Ctx.DeltaSeconds);
 *       }
 *   };
 *   App.PushLayer(std::make_unique<FWorldLayer>());
 * ```
 */
class CATTY_API FLayer
{
public:
	CATTY_DECLARE_MULTICAST_DELEGATE_OneParam(FOnAttach, FLayer&);
	CATTY_DECLARE_MULTICAST_DELEGATE_OneParam(FOnDetach, FLayer&);

	explicit FLayer(std::string Name = "Layer");
	virtual ~FLayer();

	FLayer(const FLayer&) = delete;
	FLayer& operator=(const FLayer&) = delete;

	/**
	 * Enter stack: Broadcast GetOnAttach(), then virtual OnAttach().
	 * Idempotent while already attached.
	 */
	void Attach();

	/**
	 * Leave stack: Broadcast GetOnDetach() (listeners unbind first), then virtual OnDetach().
	 * Idempotent while already detached.
	 */
	void Detach();

	[[nodiscard]] bool IsAttached() const { return bAttached; }

	[[nodiscard]] FOnAttach& GetOnAttach() { return AttachEvent; }
	[[nodiscard]] const FOnAttach& GetOnAttach() const { return AttachEvent; }
	[[nodiscard]] FOnDetach& GetOnDetach() { return DetachEvent; }
	[[nodiscard]] const FOnDetach& GetOnDetach() const { return DetachEvent; }

	/** Subclass hook after AttachEvent. Not a pipeline stage. */
	virtual void OnAttach() {}
	/** Subclass hook after DetachEvent (Post binds already cleared by listeners). */
	virtual void OnDetach() {}

	virtual void OnBeginFrame(EModuleStage Stage, FApp& App, FStageContext& Ctx)
	{
		(void)Stage;
		(void)App;
		(void)Ctx;
	}

	virtual void OnProcessInput(EModuleStage Stage, FApp& App, FStageContext& Ctx)
	{
		(void)Stage;
		(void)App;
		(void)Ctx;
	}

	virtual void OnFixedUpdate(EModuleStage Stage, FApp& App, FStageContext& Ctx)
	{
		(void)Stage;
		(void)App;
		(void)Ctx;
	}

	virtual void OnUpdate(EModuleStage Stage, FApp& App, FStageContext& Ctx)
	{
		(void)Stage;
		(void)App;
		(void)Ctx;
	}

	virtual void OnLateUpdate(EModuleStage Stage, FApp& App, FStageContext& Ctx)
	{
		(void)Stage;
		(void)App;
		(void)Ctx;
	}

	virtual void OnPreRender(EModuleStage Stage, FApp& App, FStageContext& Ctx)
	{
		(void)Stage;
		(void)App;
		(void)Ctx;
	}

	virtual void OnRender(EModuleStage Stage, FApp& App, FStageContext& Ctx)
	{
		(void)Stage;
		(void)App;
		(void)Ctx;
	}

	virtual void OnPostRender(EModuleStage Stage, FApp& App, FStageContext& Ctx)
	{
		(void)Stage;
		(void)App;
		(void)Ctx;
	}

	virtual void OnEndFrame(EModuleStage Stage, FApp& App, FStageContext& Ctx)
	{
		(void)Stage;
		(void)App;
		(void)Ctx;
	}

	[[nodiscard]] const std::string& GetName() const { return Name; }

protected:
	std::string Name;

private:
	bool bAttached = false;
	FOnAttach AttachEvent;
	FOnDetach DetachEvent;
};

} // namespace Catty
