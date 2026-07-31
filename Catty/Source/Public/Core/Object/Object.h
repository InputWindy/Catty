#pragma once

#include <Core/Export.h>
#include <Core/Object/ObjectReflect.h>

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Catty
{

class UPackage;
class FGCSystem;
class UObject;

/**
 * Intrusive refcounted handle for any UObject subclass.
 * Sole caller of UObject::AddRef / ReleaseRef (with FPropertyValue). Use Cast<T>() for typed access.
 */
class CATTY_API FObjectRef
{
public:
	FObjectRef() = default;

	FObjectRef(const FObjectRef& Other);
	FObjectRef(FObjectRef&& Other) noexcept;
	~FObjectRef();

	FObjectRef& operator=(const FObjectRef& Other);
	FObjectRef& operator=(FObjectRef&& Other) noexcept;

	[[nodiscard]] bool IsValid() const { return Object != nullptr; }
	[[nodiscard]] explicit operator bool() const { return Object != nullptr; }
	[[nodiscard]] UObject& operator*() const { return *Object; }
	[[nodiscard]] UObject* operator->() const { return Object; }

	[[nodiscard]] std::uint32_t GetRefCount() const;

	template <typename TObject>
	[[nodiscard]] TObject* Cast() const
	{
		static_assert(std::is_base_of_v<UObject, TObject>, "TObject must derive from UObject");
		return dynamic_cast<TObject*>(Object);
	}

	[[nodiscard]] bool operator==(const FObjectRef& Other) const { return Object == Other.Object; }
	[[nodiscard]] bool operator!=(const FObjectRef& Other) const { return Object != Other.Object; }

	[[nodiscard]] static FObjectRef Wrap(UObject* InObject);

	void Reset();

private:
	friend class UPackage;
	friend class FResourceSystem;
	friend class FGCSystem;
	friend class UObject;
	friend struct FPropertyValue;

	explicit FObjectRef(UObject* InObject);

	[[nodiscard]] UObject* Get() const { return Object; }

	UObject* Object = nullptr;
};

/** Object GC / lifetime flags. */
CATTY_ENUM()
enum class EObjectFlags : std::uint32_t
{
	None = 0,
	PendingKill = 1u << 0,
};

[[nodiscard]] constexpr EObjectFlags operator|(EObjectFlags A, EObjectFlags B)
{
	return static_cast<EObjectFlags>(static_cast<std::uint32_t>(A) | static_cast<std::uint32_t>(B));
}

[[nodiscard]] constexpr EObjectFlags operator&(EObjectFlags A, EObjectFlags B)
{
	return static_cast<EObjectFlags>(static_cast<std::uint32_t>(A) & static_cast<std::uint32_t>(B));
}

[[nodiscard]] constexpr EObjectFlags operator~(EObjectFlags A)
{
	return static_cast<EObjectFlags>(~static_cast<std::uint32_t>(A));
}

inline EObjectFlags& operator|=(EObjectFlags& A, EObjectFlags B)
{
	A = A | B;
	return A;
}

inline EObjectFlags& operator&=(EObjectFlags& A, EObjectFlags B)
{
	A = A & B;
	return A;
}

[[nodiscard]] constexpr bool HasAnyObjectFlags(EObjectFlags Value, EObjectFlags Test)
{
	return (static_cast<std::uint32_t>(Value) & static_cast<std::uint32_t>(Test)) != 0;
}

/**
 * Abstract base for package objects (UE UObject-lite).
 * Not allocatable by itself — construct only via subclasses (UPackage, UResource, ...).
 * Lifetime is RefCount via FObjectRef only — AddRef/ReleaseRef are private.
 * Outer is FObjectRef (empty for UPackage, or for runtime-only objects with no package).
 * Cleanup is fully owned by ~UObject.
 *
 * Reflection (editor / blueprint): CATTY_OBJECT + CATTY_PROPERTY / CATTY_FUNCTION.
 */
CATTY_OBJECT()
class CATTY_API UObject
{
	CATTY_GENERATED_BODY()

public:
	virtual ~UObject();

	UObject(const UObject&) = delete;
	UObject& operator=(const UObject&) = delete;

	// ---------------------------------------------------------------------------
	// Runtime reflect invoke (by name; not CATTY_FUNCTION markers)
	// ---------------------------------------------------------------------------
	/**
	 * Invoke a reflected function by name (walks Super chain).
	 * Args is type-erased; count must match the reflected signature.
	 * OutReturn receives the return value when the function has one (may be null).
	 */
	[[nodiscard]] bool CallFunction(
		std::string_view Name,
		const FPropertyValue* Args,
		std::size_t ArgCount,
		FPropertyValue* OutReturn = nullptr);

	[[nodiscard]] bool CallFunction(std::string_view Name)
	{
		return CallFunction(Name, static_cast<const FPropertyValue*>(nullptr), 0, nullptr);
	}

	/**
	 * Typed convenience overload. Excludes the type-erased
	 * CallFunction(Name, Args*, Count, OutReturn) form (and nullptr Args).
	 */
	template <typename TFirst, typename... TRest>
	[[nodiscard]] bool CallFunction(std::string_view Name, TFirst&& First, TRest&&... Rest)
		requires(
			!std::is_same_v<std::remove_cvref_t<TFirst>, std::nullptr_t>
			&& !std::is_convertible_v<TFirst, const FPropertyValue*>
			&& !std::is_convertible_v<TFirst, FPropertyValue*>)
	{
		const FPropertyValue Pack[] = {
			ToPropertyValue(std::forward<TFirst>(First)),
			ToPropertyValue(std::forward<TRest>(Rest))...
		};
		return CallFunction(
			Name,
			static_cast<const FPropertyValue*>(Pack),
			sizeof...(TRest) + 1,
			static_cast<FPropertyValue*>(nullptr));
	}

	[[nodiscard]] bool GetPropertyValue(std::string_view Name, FPropertyValue& OutValue) const;
	[[nodiscard]] bool SetPropertyValue(std::string_view Name, const FPropertyValue& Value);

	// ---------------------------------------------------------------------------
	// Package serialize hooks (not a GC mark graph)
	// ---------------------------------------------------------------------------
	virtual void GetReferencedObjects(std::vector<UObject*>& OutObjects) const;
	virtual void SetReferencedObjects(const std::vector<UObject*>& InObjects);

	/**
	 * FGCSystem pool TearDown — called after RefCount hits 0, before pool Free.
	 * Override for type-specific cleanup (unregister catalogs, clear tables, …).
	 * Default is no-op. Codegen registers pools with this virtual; no StaticTearDown.
	 */
	virtual void OnPoolTearDown();

	// ---------------------------------------------------------------------------
	// Reflection — CATTY_FUNCTION / CATTY_PROPERTY (game / editor / Lua)
	// ---------------------------------------------------------------------------
	CATTY_FUNCTION()
	[[nodiscard]] const std::string& GetName() const { return ObjectName; }
	CATTY_FUNCTION()
	[[nodiscard]] std::string GetPathName() const;
	CATTY_FUNCTION()
	[[nodiscard]] FObjectRef GetOuter() const;
	CATTY_FUNCTION()
	[[nodiscard]] FObjectRef GetPackage() const;
	CATTY_FUNCTION()
	[[nodiscard]] std::uint32_t GetRefCount() const { return RefCount; }
	CATTY_FUNCTION()
	[[nodiscard]] EObjectFlags GetFlags() const { return ObjectFlags; }
	CATTY_FUNCTION()
	[[nodiscard]] bool HasAnyFlags(EObjectFlags Test) const;
	CATTY_FUNCTION()
	[[nodiscard]] bool IsPendingKill() const;

protected:
	UObject(UPackage* InOuter, std::string InObjectName);

	// ---------------------------------------------------------------------------
	// Flags (subclass / package helpers — engine-only)
	// ---------------------------------------------------------------------------
	void AddFlags(EObjectFlags InFlags) { ObjectFlags |= InFlags; }
	void ClearFlags(EObjectFlags InFlags) { ObjectFlags &= ~InFlags; }

	// ---------------------------------------------------------------------------
	// Reflection — fields
	// ---------------------------------------------------------------------------
	CATTY_PROPERTY()
	std::string ObjectName;

private:
	friend class FObjectRef;
	friend class UPackage;
	friend class FResourceSystem;
	friend class FGCSystem;
	friend struct FPropertyValue;

	// ---------------------------------------------------------------------------
	// RefCount (FObjectRef / FPropertyValue only)
	// ---------------------------------------------------------------------------
	std::uint32_t AddRef();
	std::uint32_t ReleaseRef();
	void ClearOuter();

	// ---------------------------------------------------------------------------
	// Fields
	// ---------------------------------------------------------------------------
	FGCSystem* GC = nullptr;
	/** Outer package pin. Empty for UPackage, or runtime-only objects. */
	FObjectRef Outer;
	std::uint32_t RefCount = 0;
	EObjectFlags ObjectFlags = EObjectFlags::None;
};

template <typename TObject>
[[nodiscard]] TObject* Cast(const FObjectRef& Ref)
{
	return Ref.Cast<TObject>();
}

[[nodiscard]] inline FPropertyValue ToPropertyValue(const FObjectRef& Ref)
{
	return FPropertyValue::FromObject(Ref ? Ref.operator->() : nullptr);
}

[[nodiscard]] inline FObjectRef ObjectRefFromPropertyValue(const FPropertyValue& Value)
{
	return FObjectRef::Wrap(Value.GetObjectPtr());
}

} // namespace Catty
