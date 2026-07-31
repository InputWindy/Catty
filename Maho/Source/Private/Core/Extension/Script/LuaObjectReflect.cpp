#include "LuaObjectReflect.h"

#include <LuaReflectBindings.gen.h>

namespace Maho
{

void RegisterLuaObjectReflectBindings(sol::state& Lua)
{
	RegisterGeneratedLuaObjectBindings(Lua);
}

} // namespace Maho
