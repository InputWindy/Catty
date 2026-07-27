#include "Catty/Core/Reflect.h"
#include "Catty/Core/ReflectCatalog.h"
#include "Catty/Resource/Object.h"
#include "Catty/Resource/Package.h"
#include "Catty/Resource/Resource.h"

namespace Catty
{

// ReflectMeta.gen.cpp (codegen) must register these CATTY_REFLECT_CLASS types.
static_assert(TIsReflectable_v<FObject>);
static_assert(TIsReflectable_v<FPackage>);
static_assert(TIsReflectable_v<FResource>);
static_assert(ReflectCatalog::GTypeCount >= 3);

} // namespace Catty
