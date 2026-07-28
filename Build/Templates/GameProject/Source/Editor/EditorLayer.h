#pragma once

#include <Core/Layer.h>
#include <Core/Module.h>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

/**
 * Project-side editor shell (UE-inspired, ImGui docking + extensions).
 */
class FEditorLayer final : public Catty::FLayer
{
public:
	enum class EPlayState : std::uint8_t
	{
		Stopped,
		Playing,
		Paused
	};

	FEditorLayer();
	~FEditorLayer() override;

	virtual void OnAttach() override;
	virtual void OnDetach() override;

	virtual void OnUpdate(
		Catty::EModuleStage Stage,
		Catty::FApp& App,
		Catty::FStageContext& Ctx) override;

private:
	void DrawMainMenuBar(Catty::FApp& App);
	void DrawToolbar();
	void DrawDockSpace();
	void DrawViewportPanel();
	void DrawContentBrowser();
	void DrawOutputPanel(Catty::FApp& App);
	void DrawBlueprintPanel();
	void DrawPlotPanel();
	void DrawFileDialogs();

	void RefreshContentListing();
	void AppendOutput(std::string Line);
	void ExecuteConsoleLine(Catty::FApp& App, const std::string& Line);
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
	int GizmoOperation = 7; // ImGuizmo::TRANSLATE

	// Blueprint node editor
	void* NodeEditorContext = nullptr;
	bool bBlueprintInited = false;
};
