#pragma once

#include "Catty/Core/Export.h"

#include <cstdint>

namespace Catty
{

/**
 * Opaque handle for a context owned by an FThreadedServer.
 * Stale ids (after the owning task finishes) fail lookup via generation check.
 */
struct FTaskContextId
{
	std::uint32_t Index = 0;
	std::uint32_t Generation = 0;

	[[nodiscard]] bool IsValid() const { return Generation != 0; }

	bool operator==(const FTaskContextId& Other) const
	{
		return Index == Other.Index && Generation == Other.Generation;
	}

	bool operator!=(const FTaskContextId& Other) const
	{
		return !(*this == Other);
	}
};

/**
 * Base task context. Storage is allocated via FThreadedServer::AllocContext
 * and recycled automatically when the bound task finishes Execute.
 */
class CATTY_API FTaskContext
{
public:
	virtual ~FTaskContext() = default;
};

} // namespace Catty
