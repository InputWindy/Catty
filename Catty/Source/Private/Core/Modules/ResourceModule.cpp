#include <Core/Modules/ResourceModule.h>

#include <Core/App.h>
#include <Core/ConsoleManager.h>
#include <Core/GC.h>
#include <Core/Log.h>
#include <Core/Modules/GCModule.h>
#include <Core/Resource/Package.h>
#include <Core/Resource/Resource.h>

#include <algorithm>

namespace Catty
{

namespace
{

static TAutoConsoleVariable GCVarPackagePoolInitial(
	"res.Pool.PackageInitial",
	16,
	"Initial FPackage pool slot count");

static TAutoConsoleVariable GCVarResourcePoolInitial(
	"res.Pool.ResourceInitial",
	64,
	"Initial FResource pool slot count");

} // namespace

bool FResourceModule::ExecuteStage(EModuleStage Stage, FApp& App, FStageContext& Ctx)
{
	(void)Ctx;
	switch (Stage)
	{
	case EModuleStage::Init:
	{
		FGCModule* GCModule = App.GetModule<FGCModule>();
		if (!GCModule)
		{
			CATTY_CORE_ERROR("FResourceModule: GC module missing");
			return false;
		}

		FGC& GC = GCModule->GetGC();
		const int PackageSlots = (std::max)(1, GCVarPackagePoolInitial.GetValue());
		const int ResourceSlots = (std::max)(1, GCVarResourcePoolInitial.GetValue());
		GC.RegisterObjectType<FPackage>(
			static_cast<std::size_t>(PackageSlots),
			&FPackage::StaticTearDown);
		GC.RegisterObjectType<FResource>(
			static_cast<std::size_t>(ResourceSlots),
			&FResource::StaticTearDown);

		if (!ResourceManager.Initialize())
		{
			CATTY_CORE_ERROR("FResourceModule: Initialize failed");
			return false;
		}
		return true;
	}
	case EModuleStage::Shutdown:
		if (ResourceManager.IsInitialized())
		{
			ResourceManager.Shutdown();
		}
		return true;
	default:
		return true;
	}
}

} // namespace Catty
