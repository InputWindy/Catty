#include "LuaObjectReflect.h"

#include "Catty/Core/Log.h"
#include "Catty/Core/ObjectReflect.h"
#include "Catty/Resource/Object.h"
#include "Catty/Resource/ResourceManager.h"
#include "ObjectReflectTypes.gen.h"

#include <cctype>
#include <string>
#include <vector>

namespace Catty
{

namespace
{

/** Thin Lua userdata wrapper — avoids sol automagic on FObjectRef operators. */
struct FLuaObject
{
	FObjectRef Ref;

	[[nodiscard]] bool IsValid() const { return Ref.IsValid(); }
	[[nodiscard]] FObject* Get() const { return Ref ? Ref.operator->() : nullptr; }
};

[[nodiscard]] std::string SnakeToPascal(std::string_view Snake)
{
	std::string Out;
	Out.reserve(Snake.size());
	bool bCap = true;
	for (char Ch : Snake)
	{
		if (Ch == '_')
		{
			bCap = true;
			continue;
		}
		if (bCap)
		{
			Out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(Ch))));
			bCap = false;
		}
		else
		{
			Out.push_back(Ch);
		}
	}
	return Out;
}

[[nodiscard]] bool ResolveMemberName(FObject& Object, std::string_view Key, std::string& OutName, bool bFunction)
{
	const FObjectType& Type = Object.GetObjectType();
	if (bFunction)
	{
		if (Type.FindFunctionInHierarchy(Key))
		{
			OutName = std::string(Key);
			return true;
		}
		const std::string Pascal = SnakeToPascal(Key);
		if (Pascal != Key && Type.FindFunctionInHierarchy(Pascal))
		{
			OutName = Pascal;
			return true;
		}
		return false;
	}

	if (Type.FindPropertyInHierarchy(Key))
	{
		OutName = std::string(Key);
		return true;
	}
	const std::string Pascal = SnakeToPascal(Key);
	if (Pascal != Key && Type.FindPropertyInHierarchy(Pascal))
	{
		OutName = Pascal;
		return true;
	}
	return false;
}

[[nodiscard]] bool LuaToPropertyValue(const sol::object& LuaValue, EPropertyKind Expected, FPropertyValue& Out)
{
	switch (Expected)
	{
	case EPropertyKind::Bool:
		if (!LuaValue.is<bool>())
		{
			return false;
		}
		Out = FPropertyValue::FromBool(LuaValue.as<bool>());
		Out.Kind = EPropertyKind::Bool;
		return true;
	case EPropertyKind::Int32:
	case EPropertyKind::Int64:
	case EPropertyKind::UInt32:
	case EPropertyKind::EnumInt32:
		if (LuaValue.is<double>())
		{
			Out = FPropertyValue::FromInt(static_cast<std::int64_t>(LuaValue.as<double>()));
			Out.Kind = Expected;
			return true;
		}
		if (LuaValue.is<std::int64_t>())
		{
			Out = FPropertyValue::FromInt(LuaValue.as<std::int64_t>());
			Out.Kind = Expected;
			return true;
		}
		if (LuaValue.is<int>())
		{
			Out = FPropertyValue::FromInt(LuaValue.as<int>());
			Out.Kind = Expected;
			return true;
		}
		return false;
	case EPropertyKind::UInt64:
		if (LuaValue.is<double>())
		{
			Out = FPropertyValue::FromUInt(static_cast<std::uint64_t>(LuaValue.as<double>()));
			return true;
		}
		if (LuaValue.is<std::uint64_t>())
		{
			Out = FPropertyValue::FromUInt(LuaValue.as<std::uint64_t>());
			return true;
		}
		if (LuaValue.is<int>())
		{
			Out = FPropertyValue::FromUInt(static_cast<std::uint64_t>(LuaValue.as<int>()));
			return true;
		}
		return false;
	case EPropertyKind::Float:
	case EPropertyKind::Double:
		if (!LuaValue.is<double>() && !LuaValue.is<float>() && !LuaValue.is<int>())
		{
			return false;
		}
		{
			const double V = LuaValue.is<double>()
				? LuaValue.as<double>()
				: (LuaValue.is<float>() ? static_cast<double>(LuaValue.as<float>()) : static_cast<double>(LuaValue.as<int>()));
			Out = FPropertyValue::FromFloat(V);
			Out.Kind = Expected;
		}
		return true;
	case EPropertyKind::String:
		if (!LuaValue.is<std::string>())
		{
			return false;
		}
		Out = FPropertyValue::FromString(LuaValue.as<std::string>());
		return true;
	default:
		return false;
	}
}

[[nodiscard]] sol::object PropertyValueToLua(sol::state_view Lua, const FPropertyValue& Value)
{
	switch (Value.Kind)
	{
	case EPropertyKind::Bool:
		return sol::make_object(Lua, Value.BoolValue);
	case EPropertyKind::Int32:
	case EPropertyKind::Int64:
	case EPropertyKind::UInt32:
	case EPropertyKind::EnumInt32:
		return sol::make_object(Lua, static_cast<double>(Value.IntValue));
	case EPropertyKind::UInt64:
		return sol::make_object(Lua, static_cast<double>(Value.UIntValue));
	case EPropertyKind::Float:
	case EPropertyKind::Double:
		return sol::make_object(Lua, Value.FloatValue);
	case EPropertyKind::String:
		return sol::make_object(Lua, Value.StringValue);
	default:
		return sol::lua_nil;
	}
}

[[nodiscard]] sol::object CallReflected(
	sol::this_state L,
	FLuaObject Self,
	const std::string& FuncName,
	sol::variadic_args Args)
{
	sol::state_view Lua(L);
	FObject* Object = Self.Get();
	if (!Object)
	{
		return sol::lua_nil;
	}

	const FFunction* Func = Object->GetObjectType().FindFunctionInHierarchy(FuncName);
	if (!Func || !Func->Invoke)
	{
		CATTY_WARN("[Lua] unknown reflected function '{}'", FuncName);
		return sol::lua_nil;
	}

	const std::size_t ArgCount = Args.size();
	if (ArgCount != Func->ParamCount)
	{
		CATTY_WARN(
			"[Lua] {} expects {} arg(s), got {}",
			FuncName,
			Func->ParamCount,
			ArgCount);
		return sol::lua_nil;
	}

	std::vector<FPropertyValue> Pack;
	Pack.resize(ArgCount);
	for (std::size_t I = 0; I < ArgCount; ++I)
	{
		if (!LuaToPropertyValue(Args[I], Func->ParamKinds[I], Pack[I]))
		{
			CATTY_WARN("[Lua] bad arg {} for '{}'", I, FuncName);
			return sol::lua_nil;
		}
	}

	FPropertyValue OutReturn;
	const bool bOk = Object->CallFunction(
		FuncName,
		ArgCount > 0 ? Pack.data() : nullptr,
		ArgCount,
		Func->bHasReturn ? &OutReturn : nullptr);
	if (!bOk)
	{
		CATTY_WARN("[Lua] CallFunction('{}') failed", FuncName);
		return sol::lua_nil;
	}
	if (Func->bHasReturn)
	{
		return PropertyValueToLua(Lua, OutReturn);
	}
	return sol::lua_nil;
}

} // namespace

void RegisterLuaObjectReflectBindings(sol::state& Lua)
{
	EnsureObjectReflectRegistered();

	sol::usertype<FLuaObject> ObjectType = Lua.new_usertype<FLuaObject>(
		"object",
		sol::no_constructor,
		sol::meta_function::equal_to,
		[](const FLuaObject& A, const FLuaObject& B) { return A.Ref == B.Ref; },
		"is_valid",
		&FLuaObject::IsValid,
		"get_type",
		[](const FLuaObject& Self) -> std::string
		{
			FObject* Object = Self.Get();
			if (!Object || !Object->GetObjectType().Name)
			{
				return {};
			}
			return Object->GetObjectType().Name;
		});

	ObjectType[sol::meta_function::index] = [](FLuaObject& Self, sol::this_state L, const std::string& Key) -> sol::object
	{
		sol::state_view Lua(L);
		FObject* Object = Self.Get();
		if (!Object || Key.empty())
		{
			return sol::lua_nil;
		}

		std::string PropName;
		if (ResolveMemberName(*Object, Key, PropName, false))
		{
			FPropertyValue Value;
			if (Object->GetPropertyValue(PropName, Value))
			{
				return PropertyValueToLua(Lua, Value);
			}
		}

		std::string FuncName;
		if (ResolveMemberName(*Object, Key, FuncName, true))
		{
			return sol::make_object(
				Lua,
				sol::as_function(
					[Self, FuncName](sol::this_state InnerL, sol::variadic_args Args) -> sol::object
					{
						return CallReflected(InnerL, Self, FuncName, Args);
					}));
		}

		return sol::lua_nil;
	};

	ObjectType[sol::meta_function::new_index] = [](FLuaObject& Self, const std::string& Key, sol::object Value)
	{
		FObject* Object = Self.Get();
		if (!Object || Key.empty())
		{
			return;
		}
		std::string PropName;
		if (!ResolveMemberName(*Object, Key, PropName, false))
		{
			CATTY_WARN("[Lua] unknown property '{}'", Key);
			return;
		}
		const FProperty* Prop = Object->GetObjectType().FindPropertyInHierarchy(PropName);
		if (!Prop)
		{
			return;
		}
		FPropertyValue Packed;
		if (!LuaToPropertyValue(Value, Prop->Kind, Packed))
		{
			CATTY_WARN("[Lua] bad value for property '{}'", PropName);
			return;
		}
		if (!Object->SetPropertyValue(PropName, Packed))
		{
			CATTY_WARN("[Lua] SetPropertyValue('{}') failed", PropName);
		}
	};

	sol::table CattyTable = Lua["catty"];
	CattyTable["get_property"] = [](FLuaObject Self, const std::string& Name, sol::this_state L) -> sol::object
	{
		sol::state_view Lua(L);
		FObject* Object = Self.Get();
		if (!Object)
		{
			return sol::lua_nil;
		}
		std::string PropName;
		if (!ResolveMemberName(*Object, Name, PropName, false))
		{
			return sol::lua_nil;
		}
		FPropertyValue Value;
		if (!Object->GetPropertyValue(PropName, Value))
		{
			return sol::lua_nil;
		}
		return PropertyValueToLua(Lua, Value);
	};
	CattyTable["set_property"] = [](FLuaObject Self, const std::string& Name, sol::object Value) -> bool
	{
		FObject* Object = Self.Get();
		if (!Object)
		{
			return false;
		}
		std::string PropName;
		if (!ResolveMemberName(*Object, Name, PropName, false))
		{
			return false;
		}
		const FProperty* Prop = Object->GetObjectType().FindPropertyInHierarchy(PropName);
		if (!Prop)
		{
			return false;
		}
		FPropertyValue Packed;
		if (!LuaToPropertyValue(Value, Prop->Kind, Packed))
		{
			return false;
		}
		return Object->SetPropertyValue(PropName, Packed);
	};
	CattyTable["call"] = [](sol::this_state L, FLuaObject Self, const std::string& Name, sol::variadic_args Args) -> sol::object
	{
		FObject* Object = Self.Get();
		if (!Object)
		{
			return sol::lua_nil;
		}
		std::string FuncName;
		if (!ResolveMemberName(*Object, Name, FuncName, true))
		{
			CATTY_WARN("[Lua] unknown function '{}'", Name);
			return sol::lua_nil;
		}
		return CallReflected(L, Self, FuncName, Args);
	};
	CattyTable["reflect_types"] = [](sol::this_state L) -> sol::table
	{
		sol::state_view Lua(L);
		sol::table Out = Lua.create_table();
		int Index = 1;
		for (const FObjectType* Type : FObjectTypeRegistry::Get().GetTypes())
		{
			if (Type && Type->Name)
			{
				Out[Index++] = Type->Name;
			}
		}
		return Out;
	};
}

void BindLuaResourceManager(sol::state& Lua, FResourceManager& ResourceManager)
{
	sol::table CattyTable = Lua["catty"];
	CattyTable["get_transient_package"] = [&ResourceManager]() -> FLuaObject
	{
		return FLuaObject{ ResourceManager.GetTransientPackage() };
	};
	CattyTable["find_package"] = [&ResourceManager](const std::string& Name) -> FLuaObject
	{
		return FLuaObject{ ResourceManager.FindPackage(Name) };
	};
	CattyTable["find_object"] = [&ResourceManager](const std::string& PackageName, const std::string& ObjectName) -> FLuaObject
	{
		return FLuaObject{ ResourceManager.FindObject(PackageName, ObjectName) };
	};
}

} // namespace Catty
