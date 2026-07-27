#pragma once

#include "Catty/Core/Export.h"
#include "Catty/Resource/PackageRef.h"
#include "Catty/Resource/ReferenceCollector.h"

#include <cstdint>
#include <string>
#include <type_traits>

namespace Catty
{

class FPackage;
class FGCManager;

/** Object GC / lifetime flags (UE RF_* lite). */
enum class EObjectFlags : std::uint32_t
{
	None = 0,
	/** Mark bit: reachable from roots / ref seeds this collect. */
	Reachable = 1u << 0,
	/** Queued for delayed Free (purge interval). */
	PendingKill = 1u << 1,
	/** Queued for Free at end of this GC tick. */
	ImmediateDestroy = 1u << 2,
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
 * Base for package objects (UE UObject-lite).
 * Memory is allocated by owning systems (e.g. FResourceManager pools), then
 * FGCManager::RegisterObject for Mark / Root / PendingKill.
 * RefCount is bumped by FObjectRef — GC treats RefCount > 0 as live.
 * Override AddReferencedObjects to publish outbound edges for the reference graph.
 */
class CATTY_API FObject
{
public:
	FObject(FPackage* InOuter, std::string InObjectName);
	virtual ~FObject();

	FObject(const FObject&) = delete;
	FObject& operator=(const FObject&) = delete;

	[[nodiscard]] FPackageRef GetOuter() const;
	[[nodiscard]] FPackageRef GetPackage() const;
	[[nodiscard]] const std::string& GetName() const { return ObjectName; }
	[[nodiscard]] std::uint32_t GetRefCount() const { return RefCount; }
	[[nodiscard]] std::uint32_t GetRootCount() const { return RootCount; }
	[[nodiscard]] EObjectFlags GetFlags() const { return ObjectFlags; }

	[[nodiscard]] std::string GetPathName() const;

	std::uint32_t AddRef();
	std::uint32_t ReleaseRef();

	/** Keep alive without FObjectRef (UE AddToRoot). Nested calls use RootCount. */
	void AddToRoot();
	void RemoveFromRoot();
	[[nodiscard]] bool IsRooted() const { return RootCount > 0; }

	void AddFlags(EObjectFlags InFlags) { ObjectFlags |= InFlags; }
	void ClearFlags(EObjectFlags InFlags) { ObjectFlags &= ~InFlags; }
	[[nodiscard]] bool HasAnyFlags(EObjectFlags Test) const
	{
		return HasAnyObjectFlags(ObjectFlags, Test);
	}

	[[nodiscard]] bool IsPendingKill() const
	{
		return HasAnyFlags(EObjectFlags::PendingKill);
	}
	[[nodiscard]] bool IsReachable() const
	{
		return HasAnyFlags(EObjectFlags::Reachable);
	}

	/** Default GC path: delayed Free on purge. */
	void MarkPendingKill();

	/** Free at end of current FGCManager::Tick (after Mark). */
	void MarkForImmediateDestroy();

	/**
	 * Report outbound FObject references for GC Mark.
	 * Outer/Package is NOT an edge (package residency is handled separately).
	 */
	virtual void AddReferencedObjects(FReferenceCollector& Collector);

protected:
	FGCManager* GCOwner = nullptr;
	FPackage* Outer = nullptr;
	std::string ObjectName;
	std::uint32_t RefCount = 0;
	std::uint32_t RootCount = 0;
	EObjectFlags ObjectFlags = EObjectFlags::None;

	friend class FPackage;
	friend class FResourceManager;
	friend class FGCManager;
};

/**
 * Single intrusive refcounted smart pointer for all FObject subclasses.
 * Copy / assign / destroy call AddRef / ReleaseRef.
 * Public APIs return FObjectRef only — no bare FObject*. Use Cast<T>() for subtypes.
 *
 * Example:
 * ```
 *   FObjectRef Ref = ResourceManager.CreateResource(...);
 *   if (FResource* Res = Ref.Cast<FResource>())
 *   {
 *       ResourceManager.Flush(Ref);
 *   }
 * ```
 */
class FObjectRef
{
public:
	FObjectRef() = default;

	FObjectRef(const FObjectRef& Other)
		: Object(Other.Object)
	{
		if (Object)
		{
			Object->AddRef();
		}
	}

	FObjectRef(FObjectRef&& Other) noexcept
		: Object(Other.Object)
	{
		Other.Object = nullptr;
	}

	~FObjectRef()
	{
		Reset();
	}

	FObjectRef& operator=(const FObjectRef& Other)
	{
		if (this == &Other)
		{
			return *this;
		}

		if (Object)
		{
			Object->ReleaseRef();
		}

		Object = Other.Object;
		if (Object)
		{
			Object->AddRef();
		}
		return *this;
	}

	FObjectRef& operator=(FObjectRef&& Other) noexcept
	{
		if (this == &Other)
		{
			return *this;
		}

		if (Object)
		{
			Object->ReleaseRef();
		}

		Object = Other.Object;
		Other.Object = nullptr;
		return *this;
	}

	[[nodiscard]] bool IsValid() const { return Object != nullptr; }
	[[nodiscard]] explicit operator bool() const { return Object != nullptr; }
	[[nodiscard]] FObject& operator*() const { return *Object; }
	[[nodiscard]] FObject* operator->() const { return Object; }

	[[nodiscard]] std::uint32_t GetRefCount() const
	{
		return Object ? Object->GetRefCount() : 0;
	}

	/** dynamic_cast the held object to TObject*. Null if type mismatch. */
	template <typename TObject>
	[[nodiscard]] TObject* Cast() const
	{
		static_assert(std::is_base_of_v<FObject, TObject>, "TObject must derive from FObject");
		return dynamic_cast<TObject*>(Object);
	}

	[[nodiscard]] bool operator==(const FObjectRef& Other) const { return Object == Other.Object; }
	[[nodiscard]] bool operator!=(const FObjectRef& Other) const { return Object != Other.Object; }

	void Reset()
	{
		if (Object)
		{
			Object->ReleaseRef();
			Object = nullptr;
		}
	}

private:
	friend class FPackage;
	friend class FResourceManager;
	friend class FGCManager;

	/** Engine-only: wrap a pool pointer (AddRef). Games obtain Refs from Manager APIs. */
	explicit FObjectRef(FObject* InObject)
		: Object(InObject)
	{
		if (Object)
		{
			Object->AddRef();
		}
	}

	[[nodiscard]] FObject* Get() const { return Object; }

	FObject* Object = nullptr;
};

/** Free-function form of FObjectRef::Cast. */
template <typename TObject>
[[nodiscard]] TObject* Cast(const FObjectRef& Ref)
{
	return Ref.Cast<TObject>();
}

} // namespace Catty
