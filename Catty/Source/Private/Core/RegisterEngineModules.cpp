#include "Catty/Core/RegisterEngineModules.h"

#include "Catty/Core/App.h"
#include "Catty/Core/Modules/EngineModule.h"
#include "Catty/Core/Modules/GCModule.h"
#include "Catty/Core/Modules/ImGuiModule.h"
#include "Catty/Core/Modules/PlatformModule.h"
#include "Catty/Core/Modules/RenderModule.h"
#include "Catty/Core/Modules/ResourceModule.h"
#include "Catty/Core/Modules/WorkerModule.h"

#include <memory>

namespace Catty
{

void RegisterEngineModules(FApp& App)
{
	App.RegisterModule(std::make_unique<FEngineModule>());
	App.RegisterModule(std::make_unique<FPlatformModule>());
	App.RegisterModule(std::make_unique<FRenderModule>());
	App.RegisterModule(std::make_unique<FImGuiModule>());
	App.RegisterModule(std::make_unique<FGCModule>());
	App.RegisterModule(std::make_unique<FResourceModule>());
	App.RegisterModule(std::make_unique<FWorkerModule>());
}

} // namespace Catty
