#pragma once

/** Private: Lua ↔ ObjectReflect bridge (sol2). Included only from Script TUs. */

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

namespace Catty
{

void RegisterLuaObjectReflectBindings(sol::state& Lua);

} // namespace Catty
