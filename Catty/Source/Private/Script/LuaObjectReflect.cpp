#include "LuaObjectReflect.h"

#include "LuaReflectBindings.gen.h"

namespace Catty
{

void RegisterLuaObjectReflectBindings(sol::state& Lua)
{
	RegisterGeneratedLuaObjectBindings(Lua);
}

} // namespace Catty
