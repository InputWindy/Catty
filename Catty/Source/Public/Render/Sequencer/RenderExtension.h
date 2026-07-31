#pragma once

#include <Core/Export.h>
#include <Render/Sequencer/RenderStage.h>

namespace Catty
{

class FRenderServer;

/**
 * Render-pipeline feature (mirrors IEngineExtension, but driven by FRenderServer).
 * Ordering: registration order this PR; later optional TDependsOn<ERenderStage, …>.
 */
class CATTY_API IRenderExtension
{
public:
	virtual ~IRenderExtension() = default;

	[[nodiscard]] virtual const char* GetName() const = 0;

	/**
	 * Return false only for hard failure (aborts remaining stages this frame / Boot).
	 * Heavy CPU prep belongs here — not on FRHIServer.
	 */
	virtual bool ExecuteStage(ERenderStage Stage, FRenderServer& RenderServer)
	{
		(void)Stage;
		(void)RenderServer;
		return true;
	}

	[[nodiscard]] ERenderStage GetCurrentStage() const { return CurrentStage; }

private:
	friend class FRenderServer;

	void SetCurrentStage(ERenderStage Stage) { CurrentStage = Stage; }

	ERenderStage CurrentStage = ERenderStage::COUNT;
};

/**
 * Optional named helper (like FLayer for game extensions).
 */
class CATTY_API FRenderExtension : public IRenderExtension
{
public:
	explicit FRenderExtension(const char* InName = "RenderExtension")
		: Name(InName ? InName : "RenderExtension")
	{
	}

	[[nodiscard]] const char* GetName() const override { return Name; }

protected:
	const char* Name = "RenderExtension";
};

} // namespace Catty
