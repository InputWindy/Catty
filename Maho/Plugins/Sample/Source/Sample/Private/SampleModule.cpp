#include <SampleModule.h>

#include <Core/System/Log.h>

namespace Maho
{

bool FSampleModule::ExecuteStage(EEngineStage Stage)
{
	switch (Stage)
	{
	case EEngineStage::Init:
		MAHO_CORE_INFO("FSampleModule: Init");
		return true;

	case EEngineStage::Attach:
		MAHO_CORE_INFO("FSampleModule: Attach");
		return true;

	case EEngineStage::Shutdown:
		MAHO_CORE_INFO("FSampleModule: Shutdown");
		return true;

	default:
		return true;
	}
}

} // namespace Maho
