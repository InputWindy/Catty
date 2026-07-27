#pragma once

#include "Catty/Core/Export.h"
#include "Catty/Resource/ReferenceCollector.h"

#include <cstdint>
#include <string>

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
 * Base for package exports (UE UObject-lite).
 * Memory is allocated by owning systems (e.g. FResourceManager pools), then
 * FGCManager::RegisterObject for Mark / Root / PendingKill. RefCount is for handles.
 * Override AddReferencedObjects to publish outbound edges for the reference graph.
 */
class CATTY_API FObject
{
public:
	FObject(FPackage* InOuter, std::string InObjectName);
	virtual ~FObject();

	FObject(const FObject&) = delete;
	FObject& operator=(const FObject&) = delete;

	[[nodiscard]] FPackage* GetOuter() const { return Outer; }
	[[nodiscard]] FPackage* GetPackage() const { return Outer; }
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
	friend class FObjectRef;
	friend class FResourceRef;
};

class CATTY_API FObjectRef
{
public:
	FObjectRef() = default;

	explicit FObjectRef(FObject* InObject);
	FObjectRef(const FObjectRef& Other);
	FObjectRef(FObjectRef&& Other) noexcept;
	~FObjectRef();

	FObjectRef& operator=(const FObjectRef& Other);
	FObjectRef& operator=(FObjectRef&& Other) noexcept;

	[[nodiscard]] bool IsValid() const { return Object != nullptr; }
	[[nodiscard]] FObject* Get() const { return Object; }
	[[nodiscard]] FObject& operator*() const { return *Object; }
	[[nodiscard]] FObject* operator->() const { return Object; }

	void Reset();

private:
	FObject* Object = nullptr;
};

} // namespace Catty
