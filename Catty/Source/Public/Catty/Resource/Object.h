#pragma once

#include "Catty/Core/Export.h"
#include "Catty/Core/Reflect.h"

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

namespace Catty
{

class FPackage;
class FGCManager;
class FObject;

/**
 * Intrusive refcounted handle for any FObject subclass.
 * Sole caller of FObject::AddRef / ReleaseRef. Use Cast<T>() for typed access.
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

	[[nodiscard]] bool IsValid() const { return Object != nullptr; }
	[[nodiscard]] explicit operator bool() const { return Object != nullptr; }
	[[nodiscard]] FObject& operator*() const { return *Object; }
	[[nodiscard]] FObject* operator->() const { return Object; }

	[[nodiscard]] std::uint32_t GetRefCount() const;

	template <typename TObject>
	[[nodiscard]] TObject* Cast() const
	{
		static_assert(std::is_base_of_v<FObject, TObject>, "TObject must derive from FObject");
		return dynamic_cast<TObject*>(Object);
	}

	[[nodiscard]] bool operator==(const FObjectRef& Other) const { return Object == Other.Object; }
	[[nodiscard]] bool operator!=(const FObjectRef& Other) const { return Object != Other.Object; }

	[[nodiscard]] static FObjectRef Wrap(FObject* InObject);

	void Reset();

private:
	friend class FPackage;
	friend class FResourceManager;
	friend class FGCManager;
	friend class FObject;

	explicit FObjectRef(FObject* InObject);

	[[nodiscard]] FObject* Get() const { return Object; }

	FObject* Object = nullptr;
};

/** Object GC / lifetime flags. */
enum class EObjectFlags : std::uint32_t
{
	None = 0,
	PendingKill = 1u << 0,
	ImmediateDestroy = 1u << 1,
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
 * Not allocatable by itself — construct only via subclasses (FPackage, FResource, ...).
 * Lifetime is RefCount via FObjectRef only — AddRef/ReleaseRef are private.
 * Outer is FObjectRef (empty for FPackage itself). Cleanup is fully owned by ~FObject.
 */
CATTY_REFLECT_CLASS()
class CATTY_API FObject
{
public:
	virtual ~FObject();

	FObject(const FObject&) = delete;
	FObject& operator=(const FObject&) = delete;

	[[nodiscard]] FObjectRef GetOuter() const;
	[[nodiscard]] FObjectRef GetPackage() const;
	[[nodiscard]] const std::string& GetName() const { return ObjectName; }
	[[nodiscard]] std::string GetPathName() const;
	[[nodiscard]] std::uint32_t GetRefCount() const { return RefCount; }
	[[nodiscard]] EObjectFlags GetFlags() const { return ObjectFlags; }
	[[nodiscard]] bool HasAnyFlags(EObjectFlags Test) const;
	[[nodiscard]] bool IsPendingKill() const;
	[[nodiscard]] bool IsRooted() const;
	[[nodiscard]] std::uint32_t GetWeakSerial() const { return WeakSerial; }

	[[nodiscard]] static bool SplitObjectPath(
		const std::string& PathName,
		std::string& OutPackageName,
		std::string& OutObjectName);

	void AddToRoot();
	void RemoveFromRoot();

	void MarkPendingKill();
	void MarkForImmediateDestroy();

	virtual void GetReferencedObjects(std::vector<FObject*>& OutObjects) const;
	virtual void SetReferencedObjects(const std::vector<FObject*>& InObjects);

protected:
	FObject(FPackage* InOuter, std::string InObjectName);

	void AddFlags(EObjectFlags InFlags) { ObjectFlags |= InFlags; }
	void ClearFlags(EObjectFlags InFlags) { ObjectFlags &= ~InFlags; }

	std::string ObjectName;

private:
	std::uint32_t AddRef();
	std::uint32_t ReleaseRef();
	void ClearOuter();

	FGCManager* GCOwner = nullptr;
	/** Pins Outer package. Empty for FPackage itself. */
	FObjectRef Outer;
	std::uint32_t RefCount = 0;
	std::uint32_t WeakSerial = 0;
	EObjectFlags ObjectFlags = EObjectFlags::None;

	friend class FObjectRef;
	friend class FPackage;
	friend class FResourceManager;
	friend class FGCManager;
};

/** Non-owning handle; invalid after target destroy (WeakSerial mismatch). */
class FObjectWeakRef
{
public:
	FObjectWeakRef() = default;

	explicit FObjectWeakRef(FObject* InObject);
	explicit FObjectWeakRef(const FObjectRef& Ref);

	[[nodiscard]] bool IsValid() const;
	[[nodiscard]] explicit operator bool() const { return IsValid(); }

	[[nodiscard]] FObjectRef Pin() const;

	void Reset();

	[[nodiscard]] bool operator==(const FObjectWeakRef& Other) const;
	[[nodiscard]] bool operator!=(const FObjectWeakRef& Other) const;

private:
	FObject* Object = nullptr;
	std::uint32_t Serial = 0;
};

template <typename TObject>
[[nodiscard]] TObject* Cast(const FObjectRef& Ref)
{
	return Ref.Cast<TObject>();
}

} // namespace Catty
