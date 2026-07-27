#pragma once

#include "Catty/Core/Export.h"

#include <cstdint>
#include <type_traits>

namespace Catty
{

class FObject;

/**
 * Minimal reference-graph visitor (UE FReferenceCollector lite).
 * During GC Mark, AddReferencedObject enqueues outbound FObject* edges.
 *
 * Example (in a derived FObject):
 * ```
 *   void FMaterial::AddReferencedObjects(FReferenceCollector& Collector) override
 *   {
 *       FObject::AddReferencedObjects(Collector);
 *       Collector.AddReferencedObject(DiffuseTexture);
 *   }
 * ```
 */
class CATTY_API FReferenceCollector
{
public:
	virtual ~FReferenceCollector() = default;

	virtual void AddReferencedObject(FObject*& Object) = 0;

	template <typename TObject>
	void AddReferencedObject(TObject*& Object)
	{
		static_assert(std::is_base_of_v<FObject, TObject>, "TObject must derive from FObject");
		FObject* Base = Object;
		AddReferencedObject(Base);
		Object = static_cast<TObject*>(Base);
	}
};

/**
 * Soft object pointer for members that participate in the reference graph.
 * Does not AddRef; owner must report it from AddReferencedObjects.
 */
template <typename TObject>
class TObjectPtr
{
public:
	static_assert(std::is_base_of_v<FObject, TObject>, "TObject must derive from FObject");

	TObjectPtr() = default;
	TObjectPtr(TObject* InPtr) : Ptr(InPtr) {}

	TObjectPtr& operator=(TObject* InPtr)
	{
		Ptr = InPtr;
		return *this;
	}

	[[nodiscard]] TObject* Get() const { return Ptr; }
	[[nodiscard]] TObject* operator->() const { return Ptr; }
	[[nodiscard]] TObject& operator*() const { return *Ptr; }
	[[nodiscard]] explicit operator bool() const { return Ptr != nullptr; }
	[[nodiscard]] bool operator==(TObject* Other) const { return Ptr == Other; }
	[[nodiscard]] bool operator!=(TObject* Other) const { return Ptr != Other; }

	void Reset() { Ptr = nullptr; }

	void AddReferencedObject(FReferenceCollector& Collector)
	{
		Collector.AddReferencedObject(Ptr);
	}

private:
	TObject* Ptr = nullptr;
};

} // namespace Catty
