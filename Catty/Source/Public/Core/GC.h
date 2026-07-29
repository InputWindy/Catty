#pragma once

#include <Core/Export.h>
#include <Core/Object.h>
#include <Core/PoolAllocator.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Catty
{

/**
 * ConstructorFn for RegisterObjectType: NewObject uses TPoolAllocator<T>::Allocate(Args...).
 * This is how GC takes over construction for pooled FObject types.
 */
struct FPoolConstruct
{
};

inline constexpr FPoolConstruct PoolConstruct{};

/**
 * Type-erased pool + TearDown owned by FGC.
 */
class IPooledObjectType
{
public:
	virtual ~IPooledObjectType() = default;

	/** Business TearDown before pool Free. @return true if this type claimed the object. */
	[[nodiscard]] virtual bool TryTearDown(FObject* Object) = 0;
	[[nodiscard]] virtual bool TryFree(FObject* Object) = 0;
	[[nodiscard]] virtual std::size_t GetNumLive() const = 0;
	[[nodiscard]] virtual const std::type_info& GetType() const = 0;
	virtual void Clear() = 0;
};

template <typename TObject>
class TPooledObjectType final : public IPooledObjectType
{
public:
	static_assert(std::is_base_of_v<FObject, TObject>, "TObject must derive from FObject");

	using FDestroyFn = std::function<void(TObject*)>;

	TPooledObjectType(std::size_t InInitialChunkSlots, FDestroyFn InDestroyFn)
		: Pool(InInitialChunkSlots == 0 ? 1 : InInitialChunkSlots)
		, DestroyFn(std::move(InDestroyFn))
	{
	}

	template <typename... TArgs>
	[[nodiscard]] TObject* Allocate(TArgs&&... Args)
	{
		return Pool.Allocate(std::forward<TArgs>(Args)...);
	}

	bool TryTearDown(FObject* Object) override
	{
		TObject* Typed = dynamic_cast<TObject*>(Object);
		if (!Typed)
		{
			return false;
		}
		if (DestroyFn)
		{
			DestroyFn(Typed);
		}
		return true;
	}

	bool TryFree(FObject* Object) override
	{
		TObject* Typed = dynamic_cast<TObject*>(Object);
		if (!Typed)
		{
			return false;
		}
		Pool.Free(Typed);
		return true;
	}

	[[nodiscard]] std::size_t GetNumLive() const override
	{
		return Pool.GetNumLive();
	}

	[[nodiscard]] const std::type_info& GetType() const override
	{
		return typeid(TObject);
	}

	void Clear() override
	{
		Pool.Clear();
	}

private:
	TPoolAllocator<TObject> Pool;
	FDestroyFn DestroyFn;
};

/**
 * Game-thread GC (refcount scan). Owns per-type pools / TearDown.
 */
class CATTY_API FGC
{
public:
	FGC() = default;
	~FGC();

	FGC(const FGC&) = delete;
	FGC& operator=(const FGC&) = delete;

	[[nodiscard]] bool Initialize();
	void Shutdown();
	[[nodiscard]] bool IsInitialized() const { return bInitialized; }

	/**
	 * Register TObject: pool (constructor) + DestructorFn (TearDown).
	 *
	 * ConstructorFn must be PoolConstruct — NewObject<T>(Args...) calls Pool.Allocate(Args...).
	 * DestructorFn: void(TObject*) TearDown only (no Free).
	 */
	template <typename TObject, typename TConstructFn, typename TDestroyFn>
	void RegisterObjectType(
		std::size_t InitialChunkSlots,
		TConstructFn&& ConstructFn,
		TDestroyFn&& DestroyFn)
	{
		static_assert(std::is_base_of_v<FObject, TObject>, "TObject must derive from FObject");
		static_assert(
			std::is_same_v<std::decay_t<TConstructFn>, FPoolConstruct>,
			"ConstructorFn must be PoolConstruct (pooled Allocate). Custom factories can be added later.");
		(void)ConstructFn;

		typename TPooledObjectType<TObject>::FDestroyFn BoundDestroy =
			[Destroy = std::decay_t<TDestroyFn>(std::forward<TDestroyFn>(DestroyFn))](TObject* Object)
			{
				Destroy(Object);
			};

		PooledTypes[std::type_index(typeid(TObject))] =
			std::make_unique<TPooledObjectType<TObject>>(
				InitialChunkSlots,
				std::move(BoundDestroy));
	}

	template <typename TObject>
	[[nodiscard]] bool HasPooledType() const
	{
		return PooledTypes.find(std::type_index(typeid(TObject))) != PooledTypes.end();
	}

	template <typename TObject>
	[[nodiscard]] std::size_t GetPooledLiveCount() const
	{
		const auto It = PooledTypes.find(std::type_index(typeid(TObject)));
		if (It == PooledTypes.end() || !It->second)
		{
			return 0;
		}
		return It->second->GetNumLive();
	}

	template <typename TObject, typename... TArgs>
	[[nodiscard]] TObject* NewObject(TArgs&&... Args)
	{
		static_assert(std::is_base_of_v<FObject, TObject>, "TObject must derive from FObject");
		if (!bInitialized)
		{
			return nullptr;
		}

		const auto It = PooledTypes.find(std::type_index(typeid(TObject)));
		if (It == PooledTypes.end() || !It->second)
		{
			return nullptr;
		}

		auto* Entry = static_cast<TPooledObjectType<TObject>*>(It->second.get());
		TObject* Object = Entry->Allocate(std::forward<TArgs>(Args)...);
		if (!Object)
		{
			return nullptr;
		}

		RegisterObject(*Object);
		return Object;
	}

	void RegisterObject(FObject& Object);
	void UnregisterObject(FObject& Object);
	void DestroyObjectImmediate(FObject* Object);

	void CollectGarbage();
	void PurgePendingKill();
	void Tick(float DeltaSeconds);

	void SetPurgeIntervalSeconds(float Seconds) { PurgeIntervalSeconds = Seconds; }
	void SetCollectIntervalSeconds(float Seconds) { CollectIntervalSeconds = Seconds; }

private:
	friend class FObject;

	struct FRootEntry
	{
		FObjectRef Ref;
		std::uint32_t NestCount = 0;
	};

	void AddToRoot(FObject& Object);
	void RemoveFromRoot(FObject& Object);
	void EnqueuePendingKill(FObject& Object);
	void EnqueueImmediateDestroy(FObject& Object);
	[[nodiscard]] bool IsInRootSet(const FObject& Object) const;

	[[nodiscard]] static bool IsKeptAlive(const FObject& Object);

	void QueueUnreferenced();
	void ProcessImmediateDestroy();
	void RemoveAllRootRefs(FObject* Object);
	void RemoveFromPendingKill(FObject* Object);
	void RemoveFromImmediate(FObject* Object);

	[[nodiscard]] bool TearDownPooledObject(FObject* Object);
	[[nodiscard]] bool FreePooledObject(FObject* Object);

	bool bInitialized = false;

	std::unordered_map<std::type_index, std::unique_ptr<IPooledObjectType>> PooledTypes;

	std::vector<FObject*> LiveObjects;
	std::unordered_map<FObject*, FRootEntry> RootMap;
	std::vector<FObject*> PendingKill;
	std::vector<FObject*> ImmediateDestroy;

	float CollectIntervalSeconds = 1.0f;
	float CollectAccumulatorSeconds = 0.0f;
	float PurgeIntervalSeconds = 30.0f;
	float PurgeAccumulatorSeconds = 0.0f;
};

// ---------------------------------------------------------------------------
// Public free helpers. GetGC / DestroyObjectImmediate are Detail / FGC-only.
// ---------------------------------------------------------------------------

namespace Detail
{
[[nodiscard]] CATTY_API FGC* GetGC();
}

template <typename TObject, typename... TArgs>
[[nodiscard]] TObject* NewObject(TArgs&&... Args)
{
	FGC* GC = Detail::GetGC();
	if (!GC)
	{
		return nullptr;
	}
	return GC->NewObject<TObject>(std::forward<TArgs>(Args)...);
}

/**
 * Register TObject: ConstructorFn (PoolConstruct) + DestructorFn (TearDown).
 * Example:
 *   RegisterObjectType<FPackage>(16, PoolConstruct, [](FPackage* P){ TearDown(P); });
 */
template <typename TObject, typename TConstructFn, typename TDestroyFn>
void RegisterObjectType(
	std::size_t InitialChunkSlots,
	TConstructFn&& ConstructFn,
	TDestroyFn&& DestroyFn)
{
	if (FGC* GC = Detail::GetGC())
	{
		GC->RegisterObjectType<TObject>(
			InitialChunkSlots,
			std::forward<TConstructFn>(ConstructFn),
			std::forward<TDestroyFn>(DestroyFn));
	}
}

template <typename TObject>
[[nodiscard]] bool HasPooledType()
{
	FGC* GC = Detail::GetGC();
	return GC && GC->HasPooledType<TObject>();
}

template <typename TObject>
[[nodiscard]] std::size_t GetPooledLiveCount()
{
	FGC* GC = Detail::GetGC();
	return GC ? GC->GetPooledLiveCount<TObject>() : 0;
}

inline void CollectGarbage()
{
	if (FGC* GC = Detail::GetGC())
	{
		GC->CollectGarbage();
	}
}

inline void PurgePendingKill()
{
	if (FGC* GC = Detail::GetGC())
	{
		GC->PurgePendingKill();
	}
}

} // namespace Catty
