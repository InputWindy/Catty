#pragma once

#include "Catty/Platform/PlatformWindow.h"
#include "Catty/RHI/RHI.h"
#include "Catty/Server/ThreadedServer.h"

namespace Catty
{

class FVulkanRHI;

/**
 * Render server: CS worker thread + optional IRHI (Vulkan clear/present today).
 * Game thread enqueues RequestClearPresent; FApp Flushes after Render when ImGui is used.
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

	/** Vulkan RHI pointer when backend is Vulkan; otherwise nullptr. */
	[[nodiscard]] FVulkanRHI* GetVulkanRHI() const;

	/** When true, RequestClearPresent records ImGui draw data into the main pass. */
	void SetImGuiEnabled(bool bEnabled);
	[[nodiscard]] bool IsImGuiEnabled() const { return bImGuiEnabled; }

	/** Enqueue BeginFrame → main pass (clear [+ ImGui]) → EndFrame on the render thread. */
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
	bool bImGuiEnabled = false;
};

} // namespace Catty
