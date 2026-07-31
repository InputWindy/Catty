#pragma once

#include <Core/Engine.h>
#include <Core/Export.h>
#include <Core/Server/ThreadedServer.h>
#include <Core/System/PlatformWindow.h>
#include <Render/RenderFramePacket.h>
#include <Render/RHI/RHIServer.h>
#include <Render/Sequencer/RenderExtension.h>
#include <Render/Sequencer/RenderStage.h>
#include <Render/UI/ImGuiSystem.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <vector>

namespace Maho
{

class FVulkanRHI;
struct FImGuiDrawDataRing;
class FTextureProxyRegistry;

/**
 * MahoRender worker (FThreadedServer). Owned by FRenderSystem (Game).
 * Game submits work via ENQUEUE_RENDER_COMMAND; this server records/submits to FRHIServer.
 */
class MAHO_API FRenderServer final : public FThreadedServer
{
public:
	static constexpr int MaxFramesInFlightCap = 3;

	FRenderServer();
	~FRenderServer() override;

	FRenderServer(const FRenderServer&) = delete;
	FRenderServer& operator=(const FRenderServer&) = delete;

	/** Start MahoRender + RHI worker, RHI (from Window), optional ImGui. */
	[[nodiscard]] bool Boot(FPlatformWindow& InWindow, const FConfig& Config);

	void TearDown();

	/**
	 * Game: wait MaxFramesInFlight, EndFrame/Capture ImGui, enqueue ExecuteFrame on MahoRender,
	 * flush, then UpdateAndRenderPlatformWindows on Game.
	 */
	void Render(std::uint64_t FrameIndex);

	/** MahoRender: run stages + texture uploads + RHI Submit* for one packet. */
	void ExecuteFrame(FRenderFramePacket Packet);

	void WaitBeforeImGuiNewFrame(std::uint64_t FrameIndex);

	void SetClearColor(float R, float G, float B, float A);

	/** Game: queue a CPU snapshot for the next Render frame. */
	void QueueTextureUpload(FTextureCpuSnapshot Snapshot);
	void RequestTextureDestroy(std::string CatalogKey);

	[[nodiscard]] FTextureProxyRegistry& GetTextureProxyRegistry();
	[[nodiscard]] const FTextureProxyRegistry& GetTextureProxyRegistry() const;

	[[nodiscard]] FImGuiSystem& GetImGui() { return ImGui; }
	[[nodiscard]] const FImGuiSystem& GetImGui() const { return ImGui; }

	[[nodiscard]] FRHIServer& GetRHIServer() { return RHIServer; }
	[[nodiscard]] const FRHIServer& GetRHIServer() const { return RHIServer; }

	[[nodiscard]] bool HasRHI() const { return RHIServer.HasRHI(); }
	[[nodiscard]] FVulkanRHI* GetVulkanRHI() const { return RHIServer.GetVulkanRHI(); }

	void SetImGuiEnabled(bool bEnabled) { bImGuiEnabled = bEnabled; }
	[[nodiscard]] bool IsImGuiEnabled() const { return bImGuiEnabled; }

	void RequestResize(int Width, int Height) { RHIServer.RequestResize(Width, Height); }

	[[nodiscard]] FPlatformWindow* GetBoundWindow() { return BoundWindow; }
	[[nodiscard]] const FPlatformWindow* GetBoundWindow() const { return BoundWindow; }

	template <typename T, typename... TArgs>
	T& RegisterRenderExtension(TArgs&&... Args)
	{
		static_assert(std::is_base_of_v<IRenderExtension, T>, "T must derive from IRenderExtension");
		auto Extension = std::make_unique<T>(std::forward<TArgs>(Args)...);
		T& Ref = *Extension;
		RenderExtensions.push_back(std::move(Extension));
		return Ref;
	}

protected:
	[[nodiscard]] const char* GetServerThreadName() const override { return "MahoRender"; }
	[[nodiscard]] const char* GetServerLogName() const override { return "RenderServer"; }

private:
	void SyncFramebufferSize();
	[[nodiscard]] int GetMaxFramesInFlight() const;
	void ExecuteRenderStages(const FRenderFramePacket& Packet);
	[[nodiscard]] static bool InvokeRenderStage(
		IRenderExtension& Extension,
		ERenderStage Stage,
		FRenderServer& Self);

	FRHIServer RHIServer;
	FPlatformWindow* BoundWindow = nullptr;
	FImGuiSystem ImGui;
	std::unique_ptr<FImGuiDrawDataRing> ImGuiDrawDataRing;
	std::unique_ptr<FTextureProxyRegistry> TextureProxies;
	std::vector<std::unique_ptr<IRenderExtension>> RenderExtensions;

	std::mutex PendingUploadMutex;
	std::vector<FTextureCpuSnapshot> PendingTextureUploads;
	std::vector<std::string> PendingTextureDestroys;

	std::uint64_t CurrentFrameIndex = 0;

	float ClearColorR = 0.08f;
	float ClearColorG = 0.10f;
	float ClearColorB = 0.16f;
	float ClearColorA = 1.0f;

	bool bImGuiEnabled = false;
	int LastFramebufferWidth = 0;
	int LastFramebufferHeight = 0;
};

} // namespace Maho
