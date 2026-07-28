#pragma once

#include "Catty/Core/Export.h"
#include "Catty/Core/Module.h"

#include <string>

namespace Catty
{

class FApp;

/**
 * Game / editor slice bound to FApp PostStageDelegates on PushLayer / PushOverlay.
 * OnAttach / OnDetach are stack lifetime hooks, not pipeline stages.
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
	explicit FLayer(std::string Name = "Layer");
	virtual ~FLayer();

	FLayer(const FLayer&) = delete;
	FLayer& operator=(const FLayer&) = delete;

	/** Lifetime: called when pushed (before Post binds). Not a pipeline stage. */
	virtual void OnAttach() {}
	/** Lifetime: called when removed (after Post unbinds). Not a pipeline stage. */
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
};

} // namespace Catty
