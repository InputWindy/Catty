#pragma once

#include <Core/Export.h>

#include <string>

namespace Catty
{

class FPlatformWindow;
class FRHIServer;

/**
 * Dear ImGui lifecycle helper (GLFW + Vulkan backends).
 * Call BeginFrame after PollEvents (Game); build UI in TickGroups; EndFrame inside FRenderServer KickRHI.
 */
class CATTY_API FImGuiSystem
{
public:
	FImGuiSystem() = default;
	~FImGuiSystem();

	FImGuiSystem(const FImGuiSystem&) = delete;
	FImGuiSystem& operator=(const FImGuiSystem&) = delete;

	/**
	 * Requires an OS window + initialized Vulkan RHI on the RHI server.
	 * Safe to call when headless (returns false without error spam).
	 * @param ConfigDirectory Project Config/ path; imgui.ini is stored as ConfigDirectory/imgui.ini.
	 */
	[[nodiscard]] bool Initialize(
		FPlatformWindow& Window,
		FRHIServer& RHIServer,
		const std::string& ConfigDirectory = "Config");

	/** Flushes the RHI server and tears down GLFW/Vulkan backends. */
	void Shutdown(FRHIServer& RHIServer);

	[[nodiscard]] bool IsInitialized() const { return bInitialized; }

	/** ImGui_Impl*_NewFrame + ImGui::NewFrame. */
	void BeginFrame();

	/** ImGui::Render — draw data must be consumed before the next BeginFrame. */
	void EndFrame();

	/**
	 * Secondary OS viewports (drag panels outside the main window).
	 * Call on the game thread after FRHIServer has flushed this frame's main ImGui submit
	 * so GLFW + ImGui_ImplVulkan viewport work do not race the RHI thread.
	 */
	void UpdateAndRenderPlatformWindows();

	/** True when Escape was pressed and ImGui is not capturing keyboard input. */
	[[nodiscard]] bool PollExitRequest() const;

private:
	bool bInitialized = false;
	/** Owns storage for ImGuiIO::IniFilename (ImGui keeps a raw const char*). */
	std::string IniFilePath;
};

} // namespace Catty
