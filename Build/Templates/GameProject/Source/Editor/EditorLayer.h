#pragma once

#include <Core/Layer.h>
#include <Core/Module.h>

#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

/**
 * Project-side editor shell (UE-inspired ImGui layout).
 * Main menu + toolbar (Play / Pause / Stop), viewport, content browser, output + CVar line.
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
	~FEditorLayer() override = default;

	virtual void OnAttach() override;

	virtual void OnUpdate(
		Catty::EModuleStage Stage,
		Catty::FApp& App,
		Catty::FStageContext& Ctx) override;

private:
	void DrawMainMenuBar(Catty::FApp& App);
	void DrawToolbar();
	void DrawEditorShell(Catty::FApp& App);
	void DrawViewportPanel();
	void DrawContentBrowser();
	void DrawOutputPanel(Catty::FApp& App);

	void RefreshContentListing();
	void AppendOutput(std::string Line);
	void ExecuteConsoleLine(Catty::FApp& App, const std::string& Line);

	EPlayState PlayState = EPlayState::Stopped;
	bool bShowDemoWindow = false;
	bool bAutoScrollOutput = true;

	std::filesystem::path ContentRoot;
	std::filesystem::path CurrentFolder;
	std::vector<std::filesystem::path> FolderEntries;
	std::vector<std::filesystem::path> FileEntries;
	std::string SelectedEntry;

	std::deque<std::string> OutputLines;
	char ConsoleInput[512] = {};
	std::vector<std::string> ConsoleHistory;

	static constexpr std::size_t MaxOutputLines = 500;
};
