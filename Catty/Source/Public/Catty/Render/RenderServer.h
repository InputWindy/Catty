#pragma once

#include "Catty/Platform/PlatformWindow.h"
#include "Catty/RHI/RHI.h"
#include "Catty/Server/ThreadedServer.h"

namespace Catty
{

/**
 * Render server: CS worker thread + optional IRHI (Vulkan clear/present today).
 * Game thread enqueues RequestClearPresent; FApp Flushes before PreRender.
 */
class CATTY_API FRenderServer : public FThreadedServer
{
public:
	FRenderServer() = default;
	~FRenderServer() override;

	FRenderServer(const FRenderServer&) = delete;
	FRenderServer& operator=(const FRenderServer&) = delete;

	/**
	 * Create and initialize RHI for an OS window. No-op / false if headless.
	 * Must run after FThreadedServer::Initialize and platform window creation.
	 */
	[[nodiscard]] bool InitializeRHI(FPlatformWindow& Window, ERHIBackend Backend = ERHIBackend::Vulkan);

	[[nodiscard]] bool HasRHI() const;

	/** Enqueue BeginFrame → Clear → EndFrame on the render thread. */
	void RequestClearPresent(float R, float G, float B, float A = 1.0f);

	/** Enqueue resize when the framebuffer size changes. */
	void RequestResize(int Width, int Height);

protected:
	[[nodiscard]] virtual const char* GetServerThreadName() const override { return "CattyRenderThread"; }
	[[nodiscard]] virtual const char* GetServerLogName() const override { return "RenderServer"; }

	virtual bool OnInitialize() override;
	virtual void OnShutdown() override;

private:
	FRHIPtr RHI;
};

} // namespace Catty
