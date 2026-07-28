#pragma once

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
 */
class CATTY_API IModule
{
public:
	virtual ~IModule() = default;

	virtual const char* GetName() const = 0;

	/** Dependency module names (resolved after all RegisterModule calls). */
	virtual void GetDependencies(std::vector<std::string>& OutNames) const
	{
		(void)OutNames;
	}

	/**
	 * Called for every stage. Init-family may return false to abort startup.
	 */
	virtual bool OnStage(EModuleStage Stage, FApp& App, FStageContext& Ctx)
	{
		(void)Stage;
		(void)App;
		(void)Ctx;
		return true;
	}
};

} // namespace Catty
