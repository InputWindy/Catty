#pragma once

#include <Core/Extension/ResourceManager.h>
#include <Core/Server/ThreadedServer.h>

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Catty
{

/** Opaque BulkData job handle — private to FResourceServer / FResourceManager. */
struct FResourceId
{
	std::uint64_t Value = 0;

	[[nodiscard]] bool IsValid() const { return Value != 0; }

	friend bool operator==(FResourceId A, FResourceId B) { return A.Value == B.Value; }
	friend bool operator!=(FResourceId A, FResourceId B) { return A.Value != B.Value; }
};

/**
 * Private async BulkData loader for FResourceManager.
 * Issues FResourceId handles; Manager alone sees this type.
 * Does not create UResource / touch Package / catalog / Importer.
 */
class FResourceServer : public FThreadedServer
{
public:
	FResourceServer() = default;
	~FResourceServer() override;

	FResourceServer(const FResourceServer&) = delete;
	FResourceServer& operator=(const FResourceServer&) = delete;

	/** Begin async file → BulkData. Invalid id on enqueue failure. */
	[[nodiscard]] FResourceId RequestLoad(std::string Path);

	[[nodiscard]] EResourceLoadState GetLoadState(FResourceId Id) const;
	[[nodiscard]] bool IsReady(FResourceId Id) const;

	/**
	 * When Ready, move BulkData out of the registry (one-shot).
	 * Returns false if not Ready or already taken.
	 */
	[[nodiscard]] bool TryTakeBulkData(FResourceId Id, FResourceBulkData& OutBulk);

	/** Block until Id is Ready, Failed, released, or invalid. */
	void Flush(FResourceId Id);

	/** Drop a handle (and any remaining BulkData). */
	void Release(FResourceId Id);

	[[nodiscard]] bool HasPendingLoads() const;

protected:
	[[nodiscard]] virtual const char* GetServerThreadName() const override { return "CattyResourceThread"; }
	[[nodiscard]] virtual const char* GetServerLogName() const override { return "ResourceServer"; }

	virtual bool OnInitialize() override;
	virtual void OnShutdown() override;

private:
	struct FResourceRecord
	{
		std::string Path;
		EResourceLoadState State = EResourceLoadState::Invalid;
		std::vector<std::uint8_t> BulkBytes;
		bool bBulkTaken = false;
	};

	void CompleteLoad(FResourceId Id, bool bSuccess, std::vector<std::uint8_t> BulkBytes);

	mutable std::mutex RegistryMutex;
	std::condition_variable LoadCv;
	std::unordered_map<std::uint64_t, FResourceRecord> Registry;
	std::uint64_t NextResourceValue = 1;
};

} // namespace Catty
