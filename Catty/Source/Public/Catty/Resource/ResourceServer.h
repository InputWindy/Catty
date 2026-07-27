#pragma once

#include "Catty/Resource/ResourceHandle.h"
#include "Catty/Server/ThreadedServer.h"

#include <condition_variable>
#include <mutex>
#include <string>
#include <unordered_map>

namespace Catty
{

/**
 * Async raw-payload filler for FResource (same CS framework as FRenderServer).
 * FResource / FPackage objects are created on the game thread; this server only
 * loads raw source bytes into already-created resources (RequestLoad → CompleteLoad).
 * Prefer FResourceManager from game / layer code — this is the worker backend.
 *
 * Example:
 * ```
 *   ResourceServer.Initialize();
 *   const Catty::FResourceId Id = ResourceServer.RequestLoad("Meshes/Cube.mesh");
 *   ResourceServer.Flush(Id);
 *   if (ResourceServer.IsReady(Id))
 *   {
 *       std::string Path;
 *       ResourceServer.TryGetPath(Id, Path);
 *   }
 *   ResourceServer.Release(Id);
 *   ResourceServer.Shutdown();
 * ```
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

	/** Block until Id is Ready, Failed, released, or invalid (does not drain later tasks). */
	void Flush(FResourceId Id);

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
	std::condition_variable LoadCv;
	std::unordered_map<std::uint64_t, FResourceRecord> Registry;
	std::uint64_t NextResourceValue = 1;
};

} // namespace Catty
