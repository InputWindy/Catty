#pragma once

#include <Core/Resource/ResourceHandle.h>
#include <Core/Server/ThreadedServer.h>

#include <condition_variable>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Catty
{

/**
 * Internal async raw-payload filler for FResource (FThreadedServer worker).
 * Private implementation detail of FResourceManager (not a public API).
 */
class FResourceServer : public FThreadedServer
{
public:
	FResourceServer() = default;
	~FResourceServer() override;

	FResourceServer(const FResourceServer&) = delete;
	FResourceServer& operator=(const FResourceServer&) = delete;

	/**
	 * Begin an async load for Path. Returns an invalid id on failure to enqueue.
	 * Path is stored as the resource identity until a real cooker/loader exists.
	 */
	[[nodiscard]] FResourceId RequestLoad(std::string Path);

	[[nodiscard]] EResourceLoadState GetLoadState(FResourceId Id) const;
	[[nodiscard]] bool IsReady(FResourceId Id) const;
	[[nodiscard]] bool TryGetPath(FResourceId Id, std::string& OutPath) const;

	/** Block until Id is Ready, Failed, released, or invalid (does not drain later tasks). */
	void Flush(FResourceId Id);

	/** Drop a handle from the registry (safe if already invalid). */
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
	};

	void CompleteLoad(FResourceId Id, bool bSuccess);

	mutable std::mutex RegistryMutex;
	std::condition_variable LoadCv;
	std::unordered_map<std::uint64_t, FResourceRecord> Registry;
	std::uint64_t NextResourceValue = 1;
};

} // namespace Catty
