#pragma once

#include <Core/Editor/AgentChatClient.h>
#include <Core/Editor/EditorUIRegistry.h>
#include <Core/Export.h>
#include <Core/Sequencer/EngineExtension.h>
#include <Core/System/Log.h>
#include <Core/Sequencer/EngineStage.h>
#include <Render/UI/ImGuiSystem.h>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Maho
{

class FAgentChatClient;

/**
 * Engine editor shell (UE-inspired, ImGui docking + extensions).
 * Games RegisterExtension with Priority Overlay when GAME_WITH_EDITOR is enabled.
 * Chrome geometry stays here; region contributions live in FEditorUIRegistry.
 */
class MAHO_API FEditorLayer final : public FLayer
{
public:
	enum class EPlayState : std::uint8_t
	{
		Stopped,
		Playing,
		Paused
	};

	enum class EViewportTool : std::uint8_t
	{
		Select,
		Translate,
		Rotate,
		Scale
	};

	FEditorLayer();
	~FEditorLayer() override;

	bool ExecuteStage(EEngineStage Stage) override;

	[[nodiscard]] FEditorUIRegistry& GetUIRegistry() { return UIRegistry; }
	[[nodiscard]] const FEditorUIRegistry& GetUIRegistry() const { return UIRegistry; }
	[[nodiscard]] bool IsDummyUIEnabled() const { return bShowDummyUI; }

private:
	void MountEditor();
	void UnmountEditor();
	void RegisterBuiltinUIContributions();
	void RegisterDummyUIContributions();
	[[nodiscard]] FEditorUIDrawContext MakeUIDrawContext(FApp& App);

	void DrawMenuItems(FApp& App, float RowH);
	void DrawBrandBlock(float Size);
	void DrawToolbarPrimary();
	void DrawToolbarSecondary();
	// menu → toolbar1(+brand) → toolbar2 → fixed main dockspace
	void DrawDockSpace(FApp& App);
	void DrawMainViewportPanel();
	void DrawContentBrowser();
	void DrawContentBrowserTree();
	void DrawContentBrowserTiles();
	void DrawOutputPanel(FApp& App);
	void DrawAgentPanel();
	void DrawBlueprintPanel();
	void DrawSequenceGraphPanel(FApp& App);
	void DrawPlotPanel();
	void DrawTransientDetailsPanel();
	void DrawWallpaperPanel();
	void DrawFileDialogs();
	void EnsureSequenceGraphNodeLayout();
	void EnsureSequenceGraphNodeLayout(const std::vector<IEngineExtension*>& Extensions);

	void ProcessWallpaperFileDrops(FApp& App);
	[[nodiscard]] bool TryApplyWallpaperFromPath(FApp& App, const std::string& Path);
	void ClearWallpaper(FApp& App);
	void EnsureDefaultWallpaper(FApp& App);
	[[nodiscard]] static std::string ResolveDefaultWallpaperPath();

	void EnsureContentMounts();
	void SelectContentFolder(const std::string& VirtualPath);
	void RefreshContentListing();
	[[nodiscard]] std::filesystem::path VirtualPathToDisk(const std::string& VirtualPath) const;
	void DrawVirtualFolderTree(const std::string& VirtualPath, const std::filesystem::path& DiskPath, int Depth);

	void AppendOutput(std::string Line, spdlog::level::level_enum Level = spdlog::level::info);
	void DrainEngineLogs(FApp& App);
	void ExecuteConsoleLine(FApp& App, const std::string& Line);
	void EnsureDefaultDockLayout(std::uint32_t DockspaceId);

	void StartAgentChat();
	void AppendAgentBubble(EAgentChatRole Role, std::string Text);
	void SendAgentMessage(std::string Text);

	FEditorUIRegistry UIRegistry;

	EPlayState PlayState = EPlayState::Stopped;
	bool bShowDemoWindow = false;
	bool bShowImPlotDemo = false;
	bool bShowDummyUI = true;
	bool bShowContentBrowser = true;
	bool bShowOutputPanel = true;
	bool bShowAgentPanel = true;
	bool bShowBlueprintPanel = true;
	bool bShowSequenceGraphPanel = true;
	bool bShowPlotPanel = true;
	bool bShowWallpaperPanel = true;
	bool bShowTransientDetails = false;
	bool bShowDummyDockA = false;
	bool bShowDummyDockB = false;
	bool bAutoScrollOutput = true;
	bool bAutoScrollAgent = true;
	bool bBuildDefaultLayout = true;
	/** 0 = Extension depends topo (per EEngineStage), 1 = FApp::Run lifecycle. */
	int SequenceGraphViewMode = 0;
	int SequenceGraphStage = static_cast<int>(EEngineStage::BeginFrame);
	std::size_t SequenceGraphLayoutExtCount = 0;
	int SequenceGraphLayoutStage = -1;

	/** UE-style virtual path currently shown on the right (e.g. "/Game" or "/Game/Maps"). */
	std::string CurrentVirtualPath = "/Game";
	std::string SelectedVirtualEntry;
	std::vector<std::string> FolderVirtualEntries;
	std::vector<std::string> FileVirtualEntries;

	struct FOutputLine
	{
		std::string Text;
		spdlog::level::level_enum Level = spdlog::level::info;
	};

	std::deque<FOutputLine> OutputLines;
	char ConsoleInput[512] = {};
	std::vector<std::string> ConsoleHistory;
	static constexpr std::size_t MaxOutputLines = 2000;

	struct FAgentBubble
	{
		EAgentChatRole Role = EAgentChatRole::System;
		std::string Text;
	};
	std::deque<FAgentBubble> AgentBubbles;
	char AgentInput[2048] = {};
	std::unique_ptr<FAgentChatClient> AgentChat;
	static constexpr std::size_t MaxAgentBubbles = 500;

	// Viewport / ImGuizmo
	float ViewMatrix[16] = {};
	float ProjectionMatrix[16] = {};
	float ObjectMatrix[16] = {};
	EViewportTool ViewportTool = EViewportTool::Select;
	int GizmoOperation = 7; // ImGuizmo::TRANSLATE (kept in sync with ViewportTool)

	// Blueprint node editor (demo panel)
	void* NodeEditorContext = nullptr;
	bool bBlueprintInited = false;

	// SequenceGraph / FApp flow visualizer
	void* SequenceGraphEditorContext = nullptr;
	bool bSequenceGraphEditorInited = false;
	bool bSequenceGraphLayoutApplied = false;
	bool bEditorMounted = false;

	/** Desktop wallpaper shown behind translucent dock panels. */
	FImGuiTextureHandle WallpaperTexture;
	std::string WallpaperSourcePath;
	float WallpaperDropMinX = 0.0f;
	float WallpaperDropMinY = 0.0f;
	float WallpaperDropMaxX = 0.0f;
	float WallpaperDropMaxY = 0.0f;
	bool bWallpaperDropRectValid = false;
	bool bDefaultWallpaperAttempted = false;
};

} // namespace Maho
