#pragma once

#include "Catty/Server/ThreadedServer.h"

namespace Catty
{

/**
 * Render server (Vulkan-backed later).
 * Inherits the CS threaded-server framework; game/engine Enqueue render tasks,
 * FApp Flushes at the PreRender sync point.
 */
class CATTY_API FRenderServer : public FThreadedServer
{
public:
	FRenderServer() = default;
	~FRenderServer() override;

	FRenderServer(const FRenderServer&) = delete;
	FRenderServer& operator=(const FRenderServer&) = delete;

protected:
	[[nodiscard]] virtual const char* GetServerThreadName() const override { return "CattyRenderThread"; }
	[[nodiscard]] virtual const char* GetServerLogName() const override { return "RenderServer"; }

	virtual bool OnInitialize() override;
	virtual void OnShutdown() override;

private:
	bool ProbeVulkan() const;
};

} // namespace Catty
