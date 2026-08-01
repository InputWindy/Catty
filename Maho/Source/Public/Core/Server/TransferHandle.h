#pragma once

/**
 * Lightweight transfer ticket for Client → ThreadedServer blob transport.
 * Handle exposes status only (InProgress / Failed / Succeeded).
 * CatalogKey / SoftPath / Proxy stay on the client or in server registries.
 */

#include <Core/Export.h>

#include <cstdint>

namespace Maho
{

enum class ETransferState : std::uint8_t
{
	InProgress = 0,
	Failed,
	Succeeded,
};

struct MAHO_API FTransferHandle
{
	std::uint64_t Id = 0;
	std::uint32_t Generation = 0;

	[[nodiscard]] bool IsValid() const { return Id != 0; }
	[[nodiscard]] ETransferState GetState() const;
	[[nodiscard]] bool IsInProgress() const { return GetState() == ETransferState::InProgress; }
	[[nodiscard]] bool HasFailed() const { return GetState() == ETransferState::Failed; }
	[[nodiscard]] bool HasSucceeded() const { return GetState() == ETransferState::Succeeded; }
};

/** Allocate a ticket in the process-wide transfer table (used by ThreadedServer façades). */
[[nodiscard]] MAHO_API FTransferHandle AllocateTransferHandle(
	ETransferState Initial = ETransferState::InProgress);

MAHO_API void SetTransferHandleState(FTransferHandle Handle, ETransferState State);

} // namespace Maho
