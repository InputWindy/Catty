#include "LuaObjectReflect.h"

#include "Catty/Resource/ResourceManager.h"
#include "LuaReflectBindings.gen.h"

namespace Catty
{

void RegisterLuaObjectReflectBindings(sol::state& Lua)
{
	RegisterGeneratedLuaObjectBindings(Lua);
}

void BindLuaResourceManager(sol::state& Lua, FResourceManager& ResourceManager)
{
	sol::table CattyTable = Lua["catty"];
	CattyTable["get_transient_package"] = [&ResourceManager]()
	{
		return MakeLua_FPackage(ResourceManager.GetTransientPackage());
	};
	CattyTable["find_package"] = [&ResourceManager](const std::string& Name)
	{
		return MakeLua_FPackage(ResourceManager.FindPackage(Name));
	};
	CattyTable["find_object"] = [&ResourceManager](sol::this_state L, const std::string& PackageName, const std::string& ObjectName) -> sol::object
	{
		return LuaWrapObjectRef(
			sol::state_view(L),
			ResourceManager.FindObject(PackageName, ObjectName));
	};
}

} // namespace Catty
