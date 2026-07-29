#pragma once

#include <Core/Editor/AgentChatClient.h>
#include <Core/Export.h>
#include <Core/Layer.h>
#include <Core/Log.h>
#include <Core/Module.h>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Catty
{

class FAgentChatClient;

/**
 * Engine editor shell (UE-inspired, ImGui docking + extensions).
 * Games PushOverlay this when GAME_WITH_EDITOR is enabled.
 */
class CATTY_API FEditorLayer final : public FLayer
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

	virtual void OnAttach() override;
	virtual void OnDetach() override;

	virtual void OnUpdate(
		EModuleStage Stage,
		FApp& App,
		FStageContext& Ctx) override;

private:
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
	void DrawPlotPanel();
	void DrawFileDialogs();

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

	EPlayState PlayState = EPlayState::Stopped;
	bool bShowDemoWindow = false;
	bool bShowImPlotDemo = false;
	bool bShowContentBrowser = true;
	bool bShowOutputPanel = true;
	bool bShowAgentPanel = true;
	bool bShowBlueprintPanel = true;
	bool bShowPlotPanel = true;
	bool bAutoScrollOutput = true;
	bool bAutoScrollAgent = true;
	bool bBuildDefaultLayout = true;

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

	// Blueprint node editor
	void* NodeEditorContext = nullptr;
	bool bBlueprintInited = false;
};

} // namespace Catty
