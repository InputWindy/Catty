#pragma once

#include <Core/Export.h>
#include <Core/Object/ObjectReflect.h>

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace Maho
{

class UPackage;
class FGCSystem;
class UObject;

/**
 * Intrusive refcounted handle for any UObject subclass.
 */
class FObjectRef
{
public:
	FObjectRef() = default;

	FObjectRef(const FObjectRef& Other);
	FObjectRef(FObjectRef&& Other) noexcept;
	~FObjectRef();

	FObjectRef& operator=(const FObjectRef& Other);
	FObjectRef& operator=(FObjectRef&& Other) noexcept;

	[[nodiscard]] bool HasObject() const { return Object != nullptr; }

	[[nodiscard]] bool IsValid() const;
	[[nodiscard]] explicit operator bool() const { return IsValid(); }
	[[nodiscard]] UObject& operator*() const { return *Object; }
	[[nodiscard]] UObject* operator->() const { return Object; }

	[[nodiscard]] std::uint32_t GetRefCount() const;

	template <typename TObject>
	[[nodiscard]] TObject* Cast() const
	{
		static_assert(std::is_base_of_v<UObject, TObject>, "TObject must derive from UObject");
		if (!IsValid())
		{
			return nullptr;
		}
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

	[[nodiscard]] UObject* Get() const;
	[[nodiscard]] UObject* GetRaw() const { return Object; }

	UObject* Object = nullptr;
};

/** Object GC / lifetime flags. */
MAHO_ENUM()
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
 */
MAHO_OBJECT()
class UObject
{
	MAHO_GENERATED_BODY()

public:
	virtual ~UObject()
	{
		ClearOuter();
	}

	UObject(const UObject&) = delete;
	UObject& operator=(const UObject&) = delete;

	[[nodiscard]] bool CallFunction(
		std::string_view Name,
		const FPropertyValue* Args,
		std::size_t ArgCount,
		FPropertyValue* OutReturn = nullptr);

	[[nodiscard]] bool CallFunction(std::string_view Name)
	{
		return CallFunction(Name, static_cast<const FPropertyValue*>(nullptr), 0, nullptr);
	}

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

	virtual void GetReferencedObjects(std::vector<UObject*>& OutObjects) const
	{
		(void)OutObjects;
	}

	virtual void SetReferencedObjects(const std::vector<UObject*>& InObjects)
	{
		(void)InObjects;
	}

	virtual void OnPoolTearDown() {}

	MAHO_FUNCTION()
	[[nodiscard]] const std::string& GetName() const { return ObjectName; }
	MAHO_FUNCTION()
	[[nodiscard]] std::string GetPathName() const;
	MAHO_FUNCTION()
	[[nodiscard]] FObjectRef GetOuter() const { return Outer; }
	MAHO_FUNCTION()
	[[nodiscard]] FObjectRef GetPackage() const;
	MAHO_FUNCTION()
	[[nodiscard]] std::uint32_t GetRefCount() const { return RefCount; }
	MAHO_FUNCTION()
	[[nodiscard]] EObjectFlags GetFlags() const { return ObjectFlags; }
	MAHO_FUNCTION()
	[[nodiscard]] bool HasAnyFlags(EObjectFlags Test) const
	{
		return ::Maho::HasAnyObjectFlags(ObjectFlags, Test);
	}
	MAHO_FUNCTION()
	[[nodiscard]] bool IsPendingKill() const
	{
		return HasAnyFlags(EObjectFlags::PendingKill);
	}

protected:
	UObject(UPackage* InOuter, std::string InObjectName);

	void AddFlags(EObjectFlags InFlags) { ObjectFlags |= InFlags; }
	void ClearFlags(EObjectFlags InFlags) { ObjectFlags &= ~InFlags; }

	MAHO_PROPERTY()
	std::string ObjectName;

private:
	friend class FObjectRef;
	friend class UPackage;
	friend class FResourceSystem;
	friend class FGCSystem;
	friend struct FPropertyValue;

	std::uint32_t AddRef()
	{
		return ++RefCount;
	}

	std::uint32_t ReleaseRef()
	{
		if (RefCount > 0)
		{
			--RefCount;
		}
		return RefCount;
	}

	void ClearOuter()
	{
		if (Outer.Object)
		{
			Outer.Object->ReleaseRef();
			Outer.Object = nullptr;
		}
	}

	FGCSystem* GC = nullptr;
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

} // namespace Maho
