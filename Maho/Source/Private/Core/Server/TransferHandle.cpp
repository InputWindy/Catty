#include <Core/Server/TransferHandle.h>

#include <mutex>
#include <unordered_map>

namespace Maho
{

namespace
{

struct FTransferEntry
{
	std::uint32_t Generation = 0;
	ETransferState State = ETransferState::InProgress;
};

std::mutex GTransferMutex;
std::unordered_map<std::uint64_t, FTransferEntry> GTransfers;
std::uint64_t GNextTransferId = 1;

} // namespace

FTransferHandle AllocateTransferHandle(ETransferState Initial)
{
	std::lock_guard<std::mutex> Lock(GTransferMutex);

	FTransferHandle Handle{};
	Handle.Id = GNextTransferId++;
	if (Handle.Id == 0)
	{
		Handle.Id = GNextTransferId++;
	}

	FTransferEntry& Entry = GTransfers[Handle.Id];
	Entry.Generation = 1;
	Entry.State = Initial;
	Handle.Generation = Entry.Generation;
	return Handle;
}

void SetTransferHandleState(FTransferHandle Handle, ETransferState State)
{
	if (!Handle.IsValid())
	{
		return;
	}

	std::lock_guard<std::mutex> Lock(GTransferMutex);
	const auto It = GTransfers.find(Handle.Id);
	if (It == GTransfers.end() || It->second.Generation != Handle.Generation)
	{
		return;
	}
	It->second.State = State;
}

ETransferState FTransferHandle::GetState() const
{
	if (!IsValid())
	{
		return ETransferState::Failed;
	}

	std::lock_guard<std::mutex> Lock(GTransferMutex);
	const auto It = GTransfers.find(Id);
	if (It == GTransfers.end() || It->second.Generation != Generation)
	{
		return ETransferState::Failed;
	}
	return It->second.State;
}

} // namespace Maho
