#pragma once

#include "Catty/Resource/ResourceHandle.h"
#include "Catty/Server/ThreadedServer.h"

#include <mutex>
#include <string>
#include <unordered_map>

namespace Catty
{

/**
 * Async resource-load server (same CS framework as FRenderServer).
 * RequestLoad returns a handle immediately; completion runs on the resource thread.
 */
class CATTY_API FResourceServer : public FThreadedServer
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

	/** Drop a handle from the registry (safe if already invalid). */
	void Release(FResourceId Id);

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
	std::unordered_map<std::uint64_t, FResourceRecord> Registry;
	std::uint64_t NextResourceValue = 1;
};

} // namespace Catty
