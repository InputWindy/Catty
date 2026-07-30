#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

namespace Catty
{

/**
 * Growable free-list pool for a single concrete type T (placement-new / explicit dtor).
 * Not thread-safe. Not for polymorphic bases with mixed derived sizes — use one pool per T.
 *
 * Example:
 * ```
 *   Catty::TPoolAllocator<Catty::UResource> Pool(64);
 *   Catty::UResource* Res = Pool.Allocate(Outer, "T_Hero", Type, Path);
 *   Pool.Free(Res); // returns slot to free list
 * ```
 */
template <typename T>
class TPoolAllocator
{
public:
	static_assert(!std::is_array_v<T>, "TPoolAllocator does not support array types");
	static_assert(sizeof(T) > 0, "T must be a complete type");

	explicit TPoolAllocator(std::size_t InInitialChunkSlots = 32)
		: InitialChunkSlots(InInitialChunkSlots == 0 ? 1 : InInitialChunkSlots)
	{
	}

	~TPoolAllocator()
	{
		// Live objects must be Free()'d by the owner before the pool is destroyed.
		Clear();
	}

	TPoolAllocator(const TPoolAllocator&) = delete;
	TPoolAllocator& operator=(const TPoolAllocator&) = delete;

	TPoolAllocator(TPoolAllocator&& Other) noexcept
		: Chunks(std::move(Other.Chunks))
		, FreeList(std::move(Other.FreeList))
		, LiveCount(Other.LiveCount)
		, Capacity(Other.Capacity)
		, InitialChunkSlots(Other.InitialChunkSlots)
	{
		Other.LiveCount = 0;
		Other.Capacity = 0;
	}

	TPoolAllocator& operator=(TPoolAllocator&& Other) noexcept
	{
		if (this == &Other)
		{
			return *this;
		}

		Clear();
		Chunks = std::move(Other.Chunks);
		FreeList = std::move(Other.FreeList);
		LiveCount = Other.LiveCount;
		Capacity = Other.Capacity;
		InitialChunkSlots = Other.InitialChunkSlots;
		Other.LiveCount = 0;
		Other.Capacity = 0;
		return *this;
	}

	template <typename... TArgs>
	[[nodiscard]] T* Allocate(TArgs&&... Args)
	{
		if (FreeList.empty())
		{
			Grow();
		}

		void* Memory = FreeList.back();
		FreeList.pop_back();
		T* Object = ::new (Memory) T(std::forward<TArgs>(Args)...);
		++LiveCount;
		return Object;
	}

	void Free(T* Object)
	{
		if (!Object)
		{
			return;
		}

		Object->~T();
		FreeList.push_back(Object);
		if (LiveCount > 0)
		{
			--LiveCount;
		}
	}

	/** Ensure at least SlotCount slots exist (free + live). */
	void Reserve(std::size_t SlotCount)
	{
		while (Capacity < SlotCount)
		{
			Grow();
		}
	}

	/**
	 * Drop all chunk memory. Requires LiveCount == 0 (all objects Free()'d).
	 * If live objects remain, they are leaked without destructor — avoid that.
	 */
	void Clear()
	{
		FreeList.clear();
		Chunks.clear();
		LiveCount = 0;
		Capacity = 0;
	}

	[[nodiscard]] std::size_t GetNumLive() const { return LiveCount; }
	[[nodiscard]] std::size_t GetNumFree() const { return FreeList.size(); }
	[[nodiscard]] std::size_t GetCapacity() const { return Capacity; }
	[[nodiscard]] bool IsEmpty() const { return LiveCount == 0; }

	/** True if Ptr lies in a chunk owned by this pool (does not mean it is currently live). */
	[[nodiscard]] bool OwnsPointer(const T* Ptr) const
	{
		if (!Ptr)
		{
			return false;
		}

		const auto* Address = reinterpret_cast<const std::byte*>(Ptr);
		for (const FChunk& Chunk : Chunks)
		{
			if (!Chunk.AlignedBase || Chunk.SlotCount == 0)
			{
				continue;
			}

			const std::byte* Begin = Chunk.AlignedBase;
			const std::byte* End = Begin + Chunk.SlotCount * sizeof(T);
			if (Address >= Begin && Address < End)
			{
				const std::uintptr_t Offset = static_cast<std::uintptr_t>(Address - Begin);
				return (Offset % sizeof(T)) == 0;
			}
		}
		return false;
	}

private:
	struct FChunk
	{
		std::unique_ptr<std::byte[]> Memory;
		std::byte* AlignedBase = nullptr;
		std::size_t SlotCount = 0;
	};

	void Grow()
	{
		const std::size_t SlotCount = Chunks.empty()
			? InitialChunkSlots
			: Chunks.back().SlotCount * 2;

		const std::size_t Bytes = SlotCount * sizeof(T);
		const std::size_t AllocBytes = Bytes + alignof(T);

		FChunk Chunk;
		Chunk.SlotCount = SlotCount;
		Chunk.Memory = std::make_unique<std::byte[]>(AllocBytes);

		void* Aligned = Chunk.Memory.get();
		std::size_t Space = AllocBytes;
		Aligned = std::align(alignof(T), Bytes, Aligned, Space);
		Chunk.AlignedBase = static_cast<std::byte*>(Aligned);

		std::byte* Cursor = Chunk.AlignedBase;
		for (std::size_t Index = 0; Index < SlotCount; ++Index)
		{
			FreeList.push_back(Cursor + Index * sizeof(T));
		}

		Capacity += SlotCount;
		Chunks.push_back(std::move(Chunk));
	}

	std::vector<FChunk> Chunks;
	std::vector<void*> FreeList;
	std::size_t LiveCount = 0;
	std::size_t Capacity = 0;
	std::size_t InitialChunkSlots = 32;
};

} // namespace Catty
