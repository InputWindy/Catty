#pragma once

#include <Core/Export.h>
#include <Core/Layer.h>
#include <Core/Module.h>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

namespace Catty
{

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
	void DrawDocumentPanel();
	void DrawViewportPanel();
	void DrawContentBrowser();
	void DrawOutputPanel(FApp& App);
	void DrawBlueprintPanel();
	void DrawPlotPanel();
	void DrawFileDialogs();

	void RefreshContentListing();
	void AppendOutput(std::string Line);
	void ExecuteConsoleLine(FApp& App, const std::string& Line);
	void EnsureDefaultDockLayout(std::uint32_t DockspaceId);

	EPlayState PlayState = EPlayState::Stopped;
	bool bShowDemoWindow = false;
	bool bShowImPlotDemo = false;
	bool bAutoScrollOutput = true;
	bool bBuildDefaultLayout = true;

	std::filesystem::path ContentRoot;
	std::filesystem::path CurrentFolder;
	std::vector<std::filesystem::path> FolderEntries;
	std::vector<std::filesystem::path> FileEntries;
	std::string SelectedEntry;

	std::deque<std::string> OutputLines;
	char ConsoleInput[512] = {};
	std::vector<std::string> ConsoleHistory;
	static constexpr std::size_t MaxOutputLines = 500;

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
