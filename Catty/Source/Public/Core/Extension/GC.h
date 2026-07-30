#pragma once

#include <Core/DependsPack.h>
#include <Core/Export.h>
#include <Core/Sequencer/EngineExtension.h>
#include <Core/Object/Object.h>
#include <Core/Object/PoolAllocator.h>
#include <Core/TypeList.h>

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

class FGC;
class FResourceManager;
void RegisterGeneratedGCPooledTypes(FGC& GC);
void RegisterGeneratedResourceTypes(FResourceManager& Manager, FGC& GC);

/**
 * Type-erased pool + TearDown owned by FGC.
 */
class IPooledObjectType
{
public:
	virtual ~IPooledObjectType() = default;

	/** Business TearDown before pool Free. @return true if this type claimed the object. */
	[[nodiscard]] virtual bool TryTearDown(UObject* Object) = 0;
	[[nodiscard]] virtual bool TryFree(UObject* Object) = 0;
	[[nodiscard]] virtual std::size_t GetNumLive() const = 0;
	[[nodiscard]] virtual const std::type_info& GetType() const = 0;
	virtual void Clear() = 0;
};

template <typename TObject>
class TPooledObjectType final : public IPooledObjectType
{
public:
	static_assert(std::is_base_of_v<UObject, TObject>, "TObject must derive from UObject");

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

	bool TryTearDown(UObject* Object) override
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

	bool TryFree(UObject* Object) override
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
 * Built-in GC module: game-thread refcount reclaim + per-type pools.
 * Objects die only when RefCount hits 0 → CollectGarbage → PurgePendingKill.
 *
 * Public surface is application services only. Extension / pool registration are private
 * (FApp drives lifecycle via IEngineExtension*; codegen friends RegisterGenerated*).
 */
class CATTY_API FGC final
	: public IEngineExtension
	, public TDependsPack<
		TDependsOn<EEngineStage::Shutdown, TTypeList<FResourceManager>, EExtensionDepStrength::Weak>>
{
public:
	FGC() = default;
	~FGC() override;

	FGC(const FGC&) = delete;
	FGC& operator=(const FGC&) = delete;

	template <typename TObject, typename... TArgs>
	[[nodiscard]] FObjectRef NewObject(TArgs&&... Args)
	{
		static_assert(std::is_base_of_v<UObject, TObject>, "TObject must derive from UObject");
		if (!bInitialized)
		{
			return {};
		}

		const auto It = PooledTypes.find(std::type_index(typeid(TObject)));
		if (It == PooledTypes.end() || !It->second)
		{
			return {};
		}

		auto* Entry = static_cast<TPooledObjectType<TObject>*>(It->second.get());
		TObject* Object = Entry->Allocate(std::forward<TArgs>(Args)...);
		if (!Object)
		{
			return {};
		}

		RegisterObject(*Object);
		return FObjectRef::Wrap(Object);
	}

	/** Authoritative residency — LiveObjects / package table (not Resource catalog). */
	[[nodiscard]] FObjectRef FindPackage(const std::string& PackageName) const;
	[[nodiscard]] FObjectRef FindObject(
		const std::string& PackageName,
		const std::string& ObjectName) const;
	[[nodiscard]] FObjectRef FindObject(const std::string& PathName) const;

	void CollectGarbage();
	void PurgePendingKill();

	[[nodiscard]] bool IsInitialized() const { return bInitialized; }

private:
	friend void RegisterGeneratedGCPooledTypes(FGC& GC);
	friend void RegisterGeneratedResourceTypes(FResourceManager& Manager, FGC& GC);

	const char* GetName() const override { return "GC"; }

	bool ExecuteStage(EEngineStage Stage) override;
	[[nodiscard]] bool IsIdle() const override;

	[[nodiscard]] bool Initialize();
	void Shutdown();
	void Tick(float DeltaSeconds);

	/**
	 * Register TObject pool. Construction is NewObject -> Pool.Allocate -> T's ctor.
	 * TearDown invokes TObject::OnPoolTearDown() (virtual) before Free.
	 */
	template <typename TObject>
	void RegisterObjectType(std::size_t InitialChunkSlots)
	{
		static_assert(std::is_base_of_v<UObject, TObject>, "TObject must derive from UObject");

		PooledTypes[std::type_index(typeid(TObject))] =
			std::make_unique<TPooledObjectType<TObject>>(
				InitialChunkSlots,
				[](TObject* Object)
				{
					if (Object)
					{
						Object->OnPoolTearDown();
					}
				});
	}

	void RegisterObject(UObject& Object);
	void UnregisterObject(UObject& Object);

	[[nodiscard]] static bool IsKeptAlive(const UObject& Object);

	void QueueUnreferenced();
	void FinalizeDeadObject(UObject* Object);
	void RemoveFromPendingKill(UObject* Object);

	[[nodiscard]] bool TearDownPooledObject(UObject* Object);
	[[nodiscard]] bool FreePooledObject(UObject* Object);

	bool bInitialized = false;

	std::unordered_map<std::type_index, std::unique_ptr<IPooledObjectType>> PooledTypes;

	std::vector<UObject*> LiveObjects;
	std::vector<UObject*> PendingKill;

	float CollectIntervalSeconds = 1.0f;
	float CollectAccumulatorSeconds = 0.0f;
	float PurgeIntervalSeconds = 30.0f;
	float PurgeAccumulatorSeconds = 0.0f;
};

namespace Detail
{
[[nodiscard]] CATTY_API FGC* GetGC();
}

} // namespace Catty
