#pragma once

/**
 * Compile-time reflection (refl-cpp) + Lua usertype codegen.
 *
 * Recommended (UE-style): one marker on the line above the type. Codegen exports
 * every public field and public member function to ReflectCatalog, refl-cpp metadata,
 * and sol2 Lua bindings:
 * ```
 *   #include <Catty/Core/Reflect.h>
 *
 *   namespace Catty
 *   {
 *   CATTY_REFLECT_CLASS()
 *   struct FPoint
 *   {
 *   	float X = 0.f;
 *   	float Y = 0.f;
 *   	float Length() const { return X; }
 *   };
 *   }
 * ```
 *
 * Run Tools/reflect_codegen.bat (also invoked by CMake). Opt out of Lua only:
 *   CATTY_REFLECT_CLASS(CATTY_LUA_SKIP)
 *
 * Manual listing (optional, after the type) is still supported for selective export:
 *   CATTY_REFLECT_BEGIN(Catty::FPoint)
 *   	CATTY_REFLECT_PROPERTY(X)
 *   	CATTY_REFLECT_FUNCTION(Length)
 *   CATTY_REFLECT_END
 *
 * Underlying library: refl.hpp (veselink1/refl-cpp v0.12.4).
 */

#include <refl.hpp>

#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>

// -----------------------------------------------------------------------------
// Primary annotation (UE-style): line above class / struct
// -----------------------------------------------------------------------------

/**
 * Place immediately above `class` / `struct`. Expands to nothing.
 * Tools/reflect_codegen.py parses the type body and exports all **public**
 * data members and member functions (reflect meta + Lua usertype by default).
 */
#define CATTY_REFLECT_CLASS(...)

// -----------------------------------------------------------------------------
// Manual annotation macros (thin wrappers over REFL_*) — selective export
// -----------------------------------------------------------------------------

/** Begin type metadata block. Place after the class/struct definition. */
#define CATTY_REFLECT_BEGIN(TypeName, ...) REFL_TYPE(TypeName, __VA_ARGS__)

/** Reflect a data member (non-static field). */
#define CATTY_REFLECT_PROPERTY(Name, ...) REFL_FIELD(Name, __VA_ARGS__)

/** Reflect a member function (overloads OK when signatures differ). */
#define CATTY_REFLECT_FUNCTION(Name, ...) REFL_FUNC(Name, __VA_ARGS__)

/** Close CATTY_REFLECT_BEGIN block. */
#define CATTY_REFLECT_END REFL_END

/**
 * Single-shot registration (expands to REFL_AUTO).
 * Entries must use CATTY_REFLECT_TYPE / FIELD / FUNC (aliases of type/field/func).
 */
#define CATTY_REFLECT_AUTO(...) REFL_AUTO(__VA_ARGS__)

/** Entry macros for use inside CATTY_REFLECT_AUTO(...). */
#define CATTY_REFLECT_TYPE type
#define CATTY_REFLECT_FIELD field
#define CATTY_REFLECT_FUNC func

/**
 * Optional attrs for CATTY_REFLECT_CLASS(...) or manual CATTY_REFLECT_* lists.
 * Example: CATTY_REFLECT_CLASS(CATTY_LUA_SKIP) — reflect meta only, no Lua usertype.
 * CATTY_LUA_NAME is mainly for manual member lists / class-level rename.
 */
#define CATTY_LUA_SKIP ::Catty::ReflectAttr::FLuaSkip{}
#define CATTY_LUA_NAME(Name) ::Catty::ReflectAttr::FLuaName{Name}

namespace Catty
{

/** Optional attrs for CATTY_REFLECT_* (compile-time + Python Lua codegen). */
namespace ReflectAttr
{

/** Skip this type or member from generated Lua bindings (still reflected). */
struct FLuaSkip : refl::attr::usage::any
{
};

/** Override the Lua binding name (default: C++ short type / member name). */
struct FLuaName : refl::attr::usage::any
{
	const char* Value;

	constexpr explicit FLuaName(const char* InValue) noexcept
		: Value(InValue)
	{
	}
};

} // namespace ReflectAttr

/** True if T was annotated with CATTY_REFLECT_* / REFL_* (including generated meta). */
template <typename T>
inline constexpr bool TIsReflectable_v = refl::trait::is_reflectable_v<T>;

template <typename T>
struct TIsReflectable : std::bool_constant<TIsReflectable_v<T>>
{
};

/**
 * Constexpr type descriptor for a reflected T.
 * Members: .name, .members, .declared_bases, ...
 */
template <typename T>
[[nodiscard]] constexpr auto ReflectType() noexcept
{
	static_assert(TIsReflectable_v<T>, "Type is not CATTY_REFLECT_* annotated");
	return refl::reflect<T>();
}

/** Number of reflected members (fields + functions). */
template <typename T>
[[nodiscard]] constexpr std::size_t ReflectMemberCount() noexcept
{
	return ReflectType<T>().members.size;
}

/**
 * Invoke Func(MemberDescriptor) for each reflected member (constexpr-friendly).
 * MemberDescriptor exposes .name (const_string), .is_readable, .pointer, etc.
 */
template <typename T, typename F>
constexpr void ForEachReflectMember(F&& Func)
{
	refl::util::for_each(ReflectType<T>().members, std::forward<F>(Func));
}

/** Member display name as string_view (lifetime: static metadata). */
template <typename MemberDescriptor>
[[nodiscard]] constexpr std::string_view ReflectMemberName(const MemberDescriptor& Member) noexcept
{
	return std::string_view(Member.name.data, Member.name.size);
}

/** Type display name as string_view. */
template <typename T>
[[nodiscard]] constexpr std::string_view ReflectTypeName() noexcept
{
	constexpr auto Type = ReflectType<T>();
	return std::string_view(Type.name.data, Type.name.size);
}

} // namespace Catty
