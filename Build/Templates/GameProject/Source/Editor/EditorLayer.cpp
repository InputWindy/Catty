#include "Editor/EditorLayer.h"

#include <Core/App.h>
#include <Core/ConsoleManager.h>
#include <Core/Log.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <utility>

namespace
{

[[nodiscard]] std::string TrimAscii(std::string Text)
{
	while (!Text.empty() && std::isspace(static_cast<unsigned char>(Text.front())))
	{
		Text.erase(Text.begin());
	}
	while (!Text.empty() && std::isspace(static_cast<unsigned char>(Text.back())))
	{
		Text.pop_back();
	}
	return Text;
}

[[nodiscard]] const char* PlayStateLabel(FEditorLayer::EPlayState State)
{
	switch (State)
	{
	case FEditorLayer::EPlayState::Playing:
		return "Playing";
	case FEditorLayer::EPlayState::Paused:
		return "Paused";
	default:
		return "Stopped";
	}
}

} // namespace

FEditorLayer::FEditorLayer()
	: Catty::FLayer("EditorLayer")
{
}

void FEditorLayer::OnAttach()
{
	std::error_code ErrorCode;
	ContentRoot = std::filesystem::current_path(ErrorCode);
	if (ErrorCode)
	{
		ContentRoot = ".";
	}
	CurrentFolder = ContentRoot;
	RefreshContentListing();
	AppendOutput("Editor ready. Type a CVar (`Name Value`), `Name` to query, or `Dump` / `help`.");
}

void FEditorLayer::OnUpdate(
	Catty::EModuleStage /*Stage*/,
	Catty::FApp& App,
	Catty::FStageContext& /*Ctx*/)
{
	if (ImGui::GetCurrentContext() == nullptr)
	{
		return;
	}

	DrawMainMenuBar(App);

	ImGuiViewport* MainViewport = ImGui::GetMainViewport();
	const float MainMenuHeight = ImGui::GetFrameHeight();
	ImGui::SetNextWindowPos(ImVec2(MainViewport->Pos.x, MainViewport->Pos.y + MainMenuHeight));
	ImGui::SetNextWindowSize(ImVec2(MainViewport->Size.x, MainViewport->Size.y - MainMenuHeight));

	DrawEditorShell(App);

	if (bShowDemoWindow)
	{
		ImGui::ShowDemoWindow(&bShowDemoWindow);
	}
}

void FEditorLayer::DrawMainMenuBar(Catty::FApp& App)
{
	if (!ImGui::BeginMainMenuBar())
	{
		return;
	}

	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("Refresh Content Browser", "F5"))
		{
			RefreshContentListing();
			AppendOutput("Content browser refreshed.");
		}
		ImGui::Separator();
		if (ImGui::MenuItem("Exit", "Esc"))
		{
			App.OnRequestExit();
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Edit"))
	{
		ImGui::MenuItem("Undo", nullptr, false, false);
		ImGui::MenuItem("Redo", nullptr, false, false);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Window"))
	{
		ImGui::MenuItem("Output Auto-Scroll", nullptr, &bAutoScrollOutput);
		ImGui::MenuItem("ImGui Demo", nullptr, &bShowDemoWindow);
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Help"))
	{
		if (ImGui::MenuItem("CVar help"))
		{
			AppendOutput("Commands: `Dump` | `Name` | `Name Value` | `help`");
		}
		ImGui::EndMenu();
	}

	ImGui::EndMainMenuBar();
}

void FEditorLayer::DrawToolbar()
{
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));

	const float ButtonW = 72.0f;
	const float GroupW = ButtonW * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f;
	const float StartX = (std::max)(0.0f, (ImGui::GetContentRegionAvail().x - GroupW) * 0.5f);
	ImGui::SetCursorPosX(ImGui::GetCursorPosX() + StartX);

	const bool bCanPlay =
		PlayState == EPlayState::Stopped || PlayState == EPlayState::Paused;
	const bool bCanPause = PlayState == EPlayState::Playing;
	const bool bCanStop = PlayState != EPlayState::Stopped;

	ImGui::BeginDisabled(!bCanPlay);
	if (ImGui::Button("Play", ImVec2(ButtonW, 0.0f)))
	{
		PlayState = EPlayState::Playing;
		AppendOutput("PIE: Play");
		CATTY_INFO("Editor PIE -> Playing");
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::BeginDisabled(!bCanPause);
	if (ImGui::Button("Pause", ImVec2(ButtonW, 0.0f)))
	{
		PlayState = EPlayState::Paused;
		AppendOutput("PIE: Pause");
		CATTY_INFO("Editor PIE -> Paused");
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::BeginDisabled(!bCanStop);
	if (ImGui::Button("Stop", ImVec2(ButtonW, 0.0f)))
	{
		PlayState = EPlayState::Stopped;
		AppendOutput("PIE: Stop");
		CATTY_INFO("Editor PIE -> Stopped");
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	ImGui::TextDisabled("  |  %s", PlayStateLabel(PlayState));

	ImGui::PopStyleVar();
}

void FEditorLayer::DrawEditorShell(Catty::FApp& App)
{
	constexpr ImGuiWindowFlags HostFlags =
		ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoBringToFrontOnFocus
		| ImGuiWindowFlags_NoNavFocus
		| ImGuiWindowFlags_NoScrollbar
		| ImGuiWindowFlags_NoScrollWithMouse
		| ImGuiWindowFlags_MenuBar;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("##CattyEditorHost", nullptr, HostFlags);
	ImGui::PopStyleVar(3);

	// Second menu bar row (UE toolbar strip under the main menu).
	if (ImGui::BeginMenuBar())
	{
		DrawToolbar();
		ImGui::EndMenuBar();
	}

	const float BottomHeight = 240.0f;
	const float Gap = 4.0f;
	const float AvailY = ImGui::GetContentRegionAvail().y;
	const float TopHeight = (std::max)(120.0f, AvailY - BottomHeight - Gap);

	ImGui::BeginChild("##EditorTopRow", ImVec2(0.0f, TopHeight), ImGuiChildFlags_Borders);
	{
		const float ContentWidth = 280.0f;
		ImGui::BeginChild("##ContentBrowserHost", ImVec2(ContentWidth, 0.0f), ImGuiChildFlags_Borders);
		DrawContentBrowser();
		ImGui::EndChild();

		ImGui::SameLine(0.0f, Gap);

		ImGui::BeginChild("##ViewportHost", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
		DrawViewportPanel();
		ImGui::EndChild();
	}
	ImGui::EndChild();

	ImGui::BeginChild("##OutputHost", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
	DrawOutputPanel(App);
	ImGui::EndChild();

	ImGui::End();
}

void FEditorLayer::DrawViewportPanel()
{
	ImGui::TextUnformatted("Viewport");
	ImGui::Separator();

	const ImVec2 Size = ImGui::GetContentRegionAvail();
	ImGui::Dummy(ImVec2(Size.x, (std::max)(0.0f, Size.y - ImGui::GetTextLineHeightWithSpacing() * 2.0f)));

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	const ImVec2 Min = ImGui::GetItemRectMin();
	const ImVec2 Max = ImGui::GetItemRectMax();
	DrawList->AddRectFilled(Min, Max, IM_COL32(28, 30, 34, 255));
	DrawList->AddRect(Min, Max, IM_COL32(70, 74, 82, 255));

	const char* Hint = "Main Viewport (placeholder)";
	const ImVec2 TextSize = ImGui::CalcTextSize(Hint);
	DrawList->AddText(
		ImVec2(
			Min.x + (Max.x - Min.x - TextSize.x) * 0.5f,
			Min.y + (Max.y - Min.y - TextSize.y) * 0.5f),
		IM_COL32(160, 168, 180, 255),
		Hint);

	ImGui::TextDisabled("PIE: %s", PlayStateLabel(PlayState));
}

void FEditorLayer::DrawContentBrowser()
{
	ImGui::TextUnformatted("Content Browser");
	ImGui::Separator();

	if (ImGui::Button("Up"))
	{
		if (CurrentFolder != ContentRoot)
		{
			std::error_code ErrorCode;
			std::filesystem::path Parent = CurrentFolder.parent_path();
			const std::filesystem::path Rel = std::filesystem::relative(Parent, ContentRoot, ErrorCode);
			bool bUnderRoot = !ErrorCode;
			if (bUnderRoot)
			{
				for (const std::filesystem::path& Part : Rel)
				{
					if (Part == "..")
					{
						bUnderRoot = false;
						break;
					}
				}
			}
			CurrentFolder = bUnderRoot ? Parent : ContentRoot;
			RefreshContentListing();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Refresh"))
	{
		RefreshContentListing();
	}

	ImGui::TextWrapped("%s", CurrentFolder.string().c_str());
	ImGui::Separator();

	ImGui::BeginChild("##ContentList", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
	for (const std::filesystem::path& Folder : FolderEntries)
	{
		const std::string Label = std::string("[D] ") + Folder.filename().string();
		if (ImGui::Selectable(Label.c_str(), SelectedEntry == Folder.string(), ImGuiSelectableFlags_AllowDoubleClick))
		{
			SelectedEntry = Folder.string();
			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				CurrentFolder = Folder;
				RefreshContentListing();
			}
		}
	}
	for (const std::filesystem::path& File : FileEntries)
	{
		const std::string Label = std::string("[F] ") + File.filename().string();
		if (ImGui::Selectable(Label.c_str(), SelectedEntry == File.string()))
		{
			SelectedEntry = File.string();
		}
	}
	ImGui::EndChild();
}

void FEditorLayer::DrawOutputPanel(Catty::FApp& App)
{
	ImGui::TextUnformatted("Output Log");
	ImGui::SameLine();
	ImGui::TextDisabled("(CVar console)");
	ImGui::Separator();

	const float Footer = ImGui::GetFrameHeightWithSpacing() + 4.0f;
	ImGui::BeginChild(
		"##OutputScroll",
		ImVec2(0.0f, -Footer),
		ImGuiChildFlags_Borders,
		ImGuiWindowFlags_HorizontalScrollbar);

	ImGuiListClipper Clipper;
	Clipper.Begin(static_cast<int>(OutputLines.size()));
	while (Clipper.Step())
	{
		for (int Index = Clipper.DisplayStart; Index < Clipper.DisplayEnd; ++Index)
		{
			ImGui::TextUnformatted(OutputLines[static_cast<std::size_t>(Index)].c_str());
		}
	}
	if (bAutoScrollOutput && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
	{
		ImGui::SetScrollHereY(1.0f);
	}
	ImGui::EndChild();

	ImGui::SetNextItemWidth(-1.0f);
	const bool bSubmit = ImGui::InputText(
		"##ConsoleInput",
		ConsoleInput,
		IM_ARRAYSIZE(ConsoleInput),
		ImGuiInputTextFlags_EnterReturnsTrue
			| ImGuiInputTextFlags_EscapeClearsAll);
	if (bSubmit)
	{
		const std::string Line = ConsoleInput;
		ConsoleInput[0] = '\0';
		ExecuteConsoleLine(App, Line);
		ImGui::SetKeyboardFocusHere(-1);
	}
}

void FEditorLayer::RefreshContentListing()
{
	FolderEntries.clear();
	FileEntries.clear();

	std::error_code ErrorCode;
	if (!std::filesystem::exists(CurrentFolder, ErrorCode) || ErrorCode)
	{
		CurrentFolder = ContentRoot;
	}

	for (const std::filesystem::directory_entry& Entry :
		std::filesystem::directory_iterator(CurrentFolder, ErrorCode))
	{
		if (ErrorCode)
		{
			break;
		}
		const std::filesystem::path& Path = Entry.path();
		const std::string FileName = Path.filename().string();
		if (FileName.empty() || FileName[0] == '.')
		{
			continue;
		}
		if (Entry.is_directory(ErrorCode) && !ErrorCode)
		{
			FolderEntries.push_back(Path);
		}
		else if (Entry.is_regular_file(ErrorCode) && !ErrorCode)
		{
			FileEntries.push_back(Path);
		}
	}

	std::sort(FolderEntries.begin(), FolderEntries.end());
	std::sort(FileEntries.begin(), FileEntries.end());
}

void FEditorLayer::AppendOutput(std::string Line)
{
	OutputLines.push_back(std::move(Line));
	while (OutputLines.size() > MaxOutputLines)
	{
		OutputLines.pop_front();
	}
}

void FEditorLayer::ExecuteConsoleLine(Catty::FApp& App, const std::string& RawLine)
{
	const std::string Line = TrimAscii(RawLine);
	if (Line.empty())
	{
		return;
	}

	ConsoleHistory.push_back(Line);
	AppendOutput(std::string("> ") + Line);

	std::string Lower = Line;
	std::transform(Lower.begin(), Lower.end(), Lower.begin(),
		[](unsigned char Ch) { return static_cast<char>(std::tolower(Ch)); });

	if (Lower == "help" || Lower == "?")
	{
		AppendOutput("  Dump              — list registered CVars");
		AppendOutput("  <Name>            — print CVar value");
		AppendOutput("  <Name> <Value>    — set CVar (SetFromString)");
		return;
	}

	if (Lower == "dump")
	{
		Catty::FConsoleManager& Console = App.GetConsoleManager();
		const std::vector<std::string> Names = Console.GetNames();
		AppendOutput("Registered CVars (" + std::to_string(Names.size()) + "):");
		for (const std::string& CVarName : Names)
		{
			std::string Value;
			if (Console.TryGetString(CVarName.c_str(), Value))
			{
				AppendOutput("  " + CVarName + " = " + Value);
			}
			else
			{
				AppendOutput("  " + CVarName);
			}
		}
		Console.Dump();
		return;
	}

	std::string CVarName;
	std::string Value;
	{
		std::istringstream Stream(Line);
		Stream >> CVarName;
		std::getline(Stream, Value);
		Value = TrimAscii(Value);
	}

	Catty::FConsoleManager& Console = App.GetConsoleManager();
	Catty::IConsoleVariable* Variable = Console.Find(CVarName.c_str());
	if (!Variable)
	{
		AppendOutput("Unknown CVar: " + CVarName);
		CATTY_WARN("Editor console: unknown CVar '{}'", CVarName);
		return;
	}

	if (Value.empty())
	{
		AppendOutput(CVarName + " = " + Variable->GetString());
		return;
	}

	if (!Console.SetFromString(CVarName.c_str(), Value.c_str(), Catty::EConsoleVariableSetBy::Console))
	{
		AppendOutput("Failed to set " + CVarName);
		CATTY_ERROR("Editor console: failed to set '{}'", CVarName);
		return;
	}

	AppendOutput(CVarName + " = " + Variable->GetString());
	CATTY_INFO("Editor console: set {} = {}", CVarName, Variable->GetString());
}
