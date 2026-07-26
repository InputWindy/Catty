#pragma once

#include "Catty/Core/Export.h"

namespace Catty
{

class FPlatformWindow;
class FRenderServer;

/**
 * Dear ImGui lifecycle helper (GLFW + Vulkan backends).
 * Call BeginFrame after PollEvents; build UI; EndFrame before render submit.
 */
class CATTY_API FImGuiSystem
{
public:
	FImGuiSystem() = default;
	~FImGuiSystem();

	FImGuiSystem(const FImGuiSystem&) = delete;
	FImGuiSystem& operator=(const FImGuiSystem&) = delete;

	/**
	 * Requires an OS window + initialized Vulkan RHI on the render server.
	 * Safe to call when headless (returns false without error spam).
	 */
	[[nodiscard]] bool Initialize(FPlatformWindow& Window, FRenderServer& RenderServer);

	/** Flushes the render server and tears down GLFW/Vulkan backends. */
	void Shutdown(FRenderServer& RenderServer);

	[[nodiscard]] bool IsInitialized() const { return bInitialized; }

	/** ImGui_Impl*_NewFrame + ImGui::NewFrame. */
	void BeginFrame();

	/** ImGui::Render — draw data must be consumed before the next BeginFrame. */
	void EndFrame();

	/** When true, FApp::Update shows ImGui::ShowDemoWindow(). */
	bool bShowDemoWindow = true;

private:
	bool bInitialized = false;
};

} // namespace Catty
