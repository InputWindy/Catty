#pragma once

/**
 * Private async BulkData loader for FResourceSystem.
 * Uses FTransferHandle for status (InProgress / Succeeded / Failed).
 * SoftPath / CatalogKey stay on FResourceSystem — never stored here.
 */

#include <Core/Extension/Resource/Resource.h>
#include <Core/Server/ThreadedServer.h>
#include <Core/Server/TransferHandle.h>

#include <condition_variable>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Maho
{

class FResourceServer : public FThreadedServer
{
public:
	FResourceServer() = default;
	~FResourceServer() override;

	FResourceServer(const FResourceServer&) = delete;
	FResourceServer& operator=(const FResourceServer&) = delete;

	/** Begin async file → BulkData. Invalid handle on enqueue failure. */
	[[nodiscard]] FTransferHandle RequestLoad(std::string Path);

	/**
	 * When Succeeded, move BulkData out of the registry (one-shot).
	 * Returns false if not Succeeded or already taken.
	 */
	[[nodiscard]] bool TryTakeBulkData(FTransferHandle Handle, FResourceBulkData& OutBulk);

	/** Block until Handle is Succeeded, Failed, released, or invalid. */
	void Flush(FTransferHandle Handle);

	/** Drop a handle (and any remaining BulkData). */
	void Release(FTransferHandle Handle);

	[[nodiscard]] bool HasPendingLoads() const;

protected:
	[[nodiscard]] virtual const char* GetServerThreadName() const override { return "MahoResourceThread"; }
	[[nodiscard]] virtual const char* GetServerLogName() const override { return "ResourceServer"; }

	virtual bool OnInitialize() override;
	virtual void OnShutdown() override;

private:
	struct FResourceRecord
	{
		std::string Path;
		FTransferHandle Handle;
		std::vector<std::uint8_t> BulkBytes;
		EResourceBulkPreparedKind PreparedKind = EResourceBulkPreparedKind::None;
		std::shared_ptr<void> Prepared;
		bool bBulkTaken = false;
		bool bComplete = false;
	};

	void CompleteLoad(
		FTransferHandle Handle,
		bool bSuccess,
		std::vector<std::uint8_t> BulkBytes,
		EResourceBulkPreparedKind PreparedKind = EResourceBulkPreparedKind::None,
		std::shared_ptr<void> Prepared = {});

	mutable std::mutex RegistryMutex;
	std::condition_variable LoadCv;
	std::unordered_map<std::uint64_t, FResourceRecord> Registry;
};

} // namespace Maho
