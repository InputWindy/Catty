#pragma once

#include "Catty/Server/ThreadedServer.h"

namespace Catty
{

/**
 * Async resource-load server (same CS framework as FRenderServer).
 * Engine enqueues load tasks; call Flush() / future Wait(handle) when sync is required.
 */
class CATTY_API FResourceServer : public FThreadedServer
{
public:
	FResourceServer() = default;
	~FResourceServer() override;

	FResourceServer(const FResourceServer&) = delete;
	FResourceServer& operator=(const FResourceServer&) = delete;

protected:
	[[nodiscard]] virtual const char* GetServerThreadName() const override { return "CattyResourceThread"; }
	[[nodiscard]] virtual const char* GetServerLogName() const override { return "ResourceServer"; }

	virtual bool OnInitialize() override;
	virtual void OnShutdown() override;
};

} // namespace Catty
