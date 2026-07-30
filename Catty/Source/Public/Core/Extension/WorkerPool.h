#pragma once

#include <Core/Concurrent/WorkerPool.h>
#include <Core/DependsPack.h>
#include <Core/Export.h>
#include <Core/Sequencer/EngineExtension.h>
#include <Core/TypeList.h>

namespace Catty
{

class FPlatform;
class FScript;
class FGC;

/**
 * Built-in worker pool extension. Owns FWorkerPool.
 * Shutdown after Platform / Script / GC so consumers can drain first.
 */
class CATTY_API FWorkerPoolModule final
	: public IEngineExtension
	, public TDependsPack<
		TDependsOn<EEngineStage::Shutdown, TTypeList<FPlatform, FScript, FGC>, EExtensionDepStrength::Weak>>
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

} // namespace Catty
