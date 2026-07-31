#pragma once

#include <Core/Engine.h>
#include <Core/Export.h>
#include <Core/System/PlatformWindow.h>
#include <Render/RHI/RHIServer.h>
#include <Render/Sequencer/RenderExtension.h>
#include <Render/Sequencer/RenderStage.h>
#include <Render/UI/ImGuiSystem.h>

#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace Maho
{

class FVulkanRHI;
struct FImGuiDrawDataRing;

/**
 * Frame orchestration owned by FRenderSystem (Game-callable today).
 * Holds FRHIServer (RHI thread). Runs ERenderStage + IRenderExtension synchronously on Game for now.
 */
class MAHO_API FRenderServer
{
public:
	static constexpr int MaxFramesInFlightCap = 3;

	FRenderServer();
	~FRenderServer();

	FRenderServer(const FRenderServer&) = delete;
	FRenderServer& operator=(const FRenderServer&) = delete;

	/** Start RHI worker, RHI (from Window), optional ImGui. Does not create/destroy Window. */
	[[nodiscard]] bool Boot(FPlatformWindow& InWindow, const FConfig& Config);

	void TearDown();

	/**
	 * Game thread: wait MaxFramesInFlight, run ERenderStage pipeline (built-in KickRHI
	 * does clear + ImGui EndFrame/clone + Submit*). Call from FRenderSystem on EEngineStage::Render.
	 */
	void Render(std::uint64_t FrameIndex);

	/**
	 * Call on the game thread before ImGui NewFrame.
	 * ImGui Vulkan backend is not safe concurrent with RenderDrawData / clone teardown on the
	 * RHI thread — wait until the previous frame's render job has finished.
	 */
	void WaitBeforeImGuiNewFrame(std::uint64_t FrameIndex);

	void SetClearColor(float R, float G, float B, float A);

	[[nodiscard]] FImGuiSystem& GetImGui() { return ImGui; }
	[[nodiscard]] const FImGuiSystem& GetImGui() const { return ImGui; }

	[[nodiscard]] FRHIServer& GetRHIServer() { return RHIServer; }
	[[nodiscard]] const FRHIServer& GetRHIServer() const { return RHIServer; }

	[[nodiscard]] bool HasRHI() const { return RHIServer.HasRHI(); }
	[[nodiscard]] FVulkanRHI* GetVulkanRHI() const { return RHIServer.GetVulkanRHI(); }

	void SetImGuiEnabled(bool bEnabled) { bImGuiEnabled = bEnabled; }
	[[nodiscard]] bool IsImGuiEnabled() const { return bImGuiEnabled; }

	void RequestResize(int Width, int Height) { RHIServer.RequestResize(Width, Height); }

	/** Non-owning window set by Boot (for framebuffer sync). */
	[[nodiscard]] FPlatformWindow* GetBoundWindow() { return BoundWindow; }
	[[nodiscard]] const FPlatformWindow* GetBoundWindow() const { return BoundWindow; }

	/** Takes ownership. Call during Boot / game setup (before heavy ticking). */
	template <typename T, typename... TArgs>
	T& RegisterRenderExtension(TArgs&&... Args)
	{
		static_assert(std::is_base_of_v<IRenderExtension, T>, "T must derive from IRenderExtension");
		auto Extension = std::make_unique<T>(std::forward<TArgs>(Args)...);
		T& Ref = *Extension;
		RenderExtensions.push_back(std::move(Extension));
		return Ref;
	}

private:
	void SyncFramebufferSize();
	[[nodiscard]] int GetMaxFramesInFlight() const;
	void ExecuteRenderStages();
	[[nodiscard]] static bool InvokeRenderStage(
		IRenderExtension& Extension,
		ERenderStage Stage,
		FRenderServer& Self);

	FRHIServer RHIServer;
	FPlatformWindow* BoundWindow = nullptr;
	FImGuiSystem ImGui;
	std::unique_ptr<FImGuiDrawDataRing> ImGuiDrawDataRing;
	std::vector<std::unique_ptr<IRenderExtension>> RenderExtensions;

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

} // namespace Maho
