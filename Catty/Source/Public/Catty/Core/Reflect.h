#pragma once

/**
 * Compile-time reflection (refl-cpp) — core metadata for types / fields / methods.
 *
 * Annotate after the class definition (header or .cpp). Metadata is constexpr;
 * no runtime registry. Later consumers (Lua export, serializers) walk descriptors.
 *
 * Procedural form (recommended):
 * ```
 *   namespace Catty
 *   {
 *       struct FPoint
 *       {
 *           float X = 0.f;
 *           float Y = 0.f;
 *           float Length() const { return X; }
 *       };
 *   }
 *
 *   CATTY_REFLECT_BEGIN(Catty::FPoint)
 *   	CATTY_REFLECT_PROPERTY(X)
 *   	CATTY_REFLECT_PROPERTY(Y)
 *   	CATTY_REFLECT_FUNCTION(Length)
 *   CATTY_REFLECT_END
 *
 *   static_assert(Catty::TIsReflectable_v<Catty::FPoint>);
 *   constexpr auto Type = Catty::ReflectType<Catty::FPoint>();
 * ```
 *
 * Auto form (one macro, REFL_AUTO style):
 * ```
 *   CATTY_REFLECT_AUTO(
 *   	CATTY_REFLECT_TYPE(Catty::FPoint),
 *   	CATTY_REFLECT_FIELD(X),
 *   	CATTY_REFLECT_FIELD(Y),
 *   	CATTY_REFLECT_FUNC(Length)
 *   )
 * ```
 *
 * Optional attributes (passthrough to refl-cpp), e.g. bases:
 * ```
 *   CATTY_REFLECT_BEGIN(Catty::FDerived, bases<Catty::FBase>)
 *   ...
 *   CATTY_REFLECT_END
 * ```
 *
 * Underlying library: refl.hpp (veselink1/refl-cpp v0.12.4).
 */

#include <refl.hpp>

#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>

// -----------------------------------------------------------------------------
// Annotation macros (thin wrappers over REFL_*)
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

namespace Catty
{

/** True if T was annotated with CATTY_REFLECT_* / REFL_*. */
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
