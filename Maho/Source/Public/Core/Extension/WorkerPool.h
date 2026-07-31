#pragma once

#include <Core/Concurrent/WorkerPool.h>
#include <Core/DependsPack.h>
#include <Core/Export.h>
#include <Core/Sequencer/EngineExtension.h>
#include <Core/TypeList.h>

namespace Maho
{

class FPlatformSystem;
class FScriptSystem;
class FGCSystem;

/**
 * Built-in worker pool extension. Owns FWorkerPool.
 * Shutdown after Platform / Script / GC so consumers can drain first.
 */
class MAHO_API FWorkerPoolSystem final
	: public IEngineExtension
	, public TDependsPack<
		TDependsOn<EEngineStage::Shutdown, TTypeList<FPlatformSystem, FScriptSystem, FGCSystem>, EExtensionDepStrength::Weak>>
{
public:
	[[nodiscard]] FWorkerPool& GetPool() { return Pool; }
	[[nodiscard]] const FWorkerPool& GetPool() const { return Pool; }

private:
	const char* GetName() const override { return "WorkerPool"; }

	bool ExecuteStage(EEngineStage Stage) override;
	[[nodiscard]] bool IsIdle() const override;

	FWorkerPool Pool;
};

} // namespace Maho
