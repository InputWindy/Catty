#include <SampleModule.h>

#include <Core/System/Log.h>

namespace Catty
{

bool FSampleModule::ExecuteStage(EEngineStage Stage)
{
	switch (Stage)
	{
	case EEngineStage::Init:
		CATTY_CORE_INFO("FSampleModule: Init");
		return true;

	case EEngineStage::Attach:
		CATTY_CORE_INFO("FSampleModule: Attach");
		return true;

	case EEngineStage::Shutdown:
		CATTY_CORE_INFO("FSampleModule: Shutdown");
		return true;

	default:
		return true;
	}
}

} // namespace Catty
