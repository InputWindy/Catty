#pragma once

#include <Core/Engine.h>
#include <Core/Export.h>
#include <Core/Server/ThreadedServer.h>
#include <Core/System/PlatformWindow.h>
#include <Render/RHI/RHI.h>
#include <Render/UI/ImGuiSystem.h>

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>

namespace Catty
{

class FVulkanRHI;
struct FImGuiDrawDataRing;

/**
 * RenderCore service owned by FRender module. Does not own the platform window.
 * Boot borrows FPlatformWindow&; TearDown leaves the window alone.
 * Render(FrameIndex): fence gate, default clear, ImGui submit, End+fence.
 * FApp::Tick runs TickGroups: BeginFrame…EndFrame → PreRender → Render → PostRender.
 * FRender handles ImGui NewFrame on BeginFrame and submit on Render.
 */
class CATTY_API FRenderServer : public FThreadedServer
{
public:
	static constexpr int MaxFramesInFlightCap = 3;

	FRenderServer();
	~FRenderServer() override;

	FRenderServer(const FRenderServer&) = delete;
	FRenderServer& operator=(const FRenderServer&) = delete;

	/** Start worker, RHI (from Window), optional ImGui. Does not create/destroy Window. */
	[[nodiscard]] bool Boot(FPlatformWindow& InWindow, const FEngineConfig& Config);

	void TearDown();

	/**
	 * Game thread: wait MaxFramesInFlight, sync resize, default clear + ImGui EndFrame/clone,
	 * enqueue UI/End, signal fence. Call from FRender on EEngineStage::Render
	 * (ImGui NewFrame already done on BeginFrame).
	 */
	void Render(std::uint64_t FrameIndex);

	/**
	 * Call on the game thread before ImGui NewFrame.
	 * ImGui Vulkan backend is not safe concurrent with RenderDrawData / clone teardown on the
	 * render thread — wait until the previous frame's render job has finished.
	 */
	void WaitBeforeImGuiNewFrame(std::uint64_t FrameIndex);

	void SetClearColor(float R, float G, float B, float A);

	[[nodiscard]] FImGuiSystem& GetImGui() { return ImGui; }
	[[nodiscard]] const FImGuiSystem& GetImGui() const { return ImGui; }

	[[nodiscard]] bool HasRHI() const { return static_cast<bool>(RHI); }
	[[nodiscard]] FVulkanRHI* GetVulkanRHI() const;

	void SetImGuiEnabled(bool bEnabled) { bImGuiEnabled = bEnabled; }
	[[nodiscard]] bool IsImGuiEnabled() const { return bImGuiEnabled; }

	void RequestResize(int Width, int Height);

	/** Non-owning window set by Boot (for framebuffer sync). */
	[[nodiscard]] FPlatformWindow* GetBoundWindow() { return BoundWindow; }
	[[nodiscard]] const FPlatformWindow* GetBoundWindow() const { return BoundWindow; }

protected:
	[[nodiscard]] const char* GetServerThreadName() const override { return "CattyRenderCore"; }
	[[nodiscard]] const char* GetServerLogName() const override { return "RenderServer"; }

	bool OnInitialize() override;
	void OnShutdown() override;

private:
	void SyncFramebufferSize();
	void WaitForRenderFrame(std::uint64_t FrameIndex);
	void SignalRenderFrameComplete(std::uint64_t FrameIndex);
	[[nodiscard]] int GetMaxFramesInFlight() const;
	void SubmitBeginMainPass(float R, float G, float B, float A);
	void SubmitRenderUI(int SlotIndex);
	void SubmitEndFrameAndFence(std::uint64_t FrameIndex);
	[[nodiscard]] bool InitializeRHI(FPlatformWindow& InWindow, ERHIBackend Backend = ERHIBackend::Vulkan);

	FPlatformWindow* BoundWindow = nullptr;
	FImGuiSystem ImGui;
	FRHIPtr RHI;
	std::unique_ptr<FImGuiDrawDataRing> ImGuiDrawDataRing;

	std::mutex FenceMutex;
	std::condition_variable FenceCv;
	std::uint64_t LastCompletedRenderFrame = 0;
	std::uint64_t CurrentFrameIndex = 0;
	int PendingImGuiSlotIndex = -1;

	float ClearColorR = 0.08f;
	float ClearColorG = 0.10f;
	float ClearColorB = 0.16f;
	float ClearColorA = 1.0f;

	bool bImGuiEnabled = false;
	int LastFramebufferWidth = 0;
	int LastFramebufferHeight = 0;
};

} // namespace Catty
