#pragma once

#include "Catty/Core/Export.h"

#include <cstdint>
#include <string>

namespace Catty
{

class FPackage;

/**
 * Base for package exports (UE UObject-lite).
 * Every instance is uniquely bound to one FPackage (Outer) and has an ObjectName
 * unique within that package. Identity = PackageName.ObjectName.
 *
 * RefCount is adjusted by FObjectRef / FResourceRef — Package ownership is separate
 * (unique_ptr in FPackage export table).
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

	/** Full path: "PackageName.ObjectName" (empty package name omitted). */
	[[nodiscard]] std::string GetPathName() const;

	/** @return new refcount after increment. */
	std::uint32_t AddRef();

	/** @return new refcount after decrement. */
	std::uint32_t ReleaseRef();

protected:
	FPackage* Outer = nullptr;
	std::string ObjectName;
	std::uint32_t RefCount = 0;

	friend class FPackage;
	friend class FResourceManager;
	friend class FObjectRef;
	friend class FResourceRef;
};

/**
 * Refcounted handle to FObject (copy/assign AddRef, dtor ReleaseRef).
 */
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
