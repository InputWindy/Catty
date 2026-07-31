#include <Core/Extension/WorkerPool/WorkerPool.h>

#include <Core/System/Log.h>

namespace Maho
{

bool FWorkerPoolSystem::ExecuteStage(EEngineStage Stage)
{
	switch (Stage)
	{
	case EEngineStage::Init:
		if (!Pool.Initialize())
		{
			MAHO_CORE_ERROR("FWorkerPoolSystem: Initialize failed");
			return false;
		}
		return true;
	case EEngineStage::Shutdown:
		if (Pool.IsInitialized())
		{
			Pool.Shutdown();
		}
		return true;
	default:
		return true;
	}
}

bool FWorkerPoolSystem::IsIdle() const
{
	return !Pool.IsInitialized() || Pool.IsIdle();
}

} // namespace Maho
