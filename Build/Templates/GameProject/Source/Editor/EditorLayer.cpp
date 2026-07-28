#include "Editor/EditorLayer.h"

#include <Core/App.h>
#include <Core/ConsoleManager.h>
#include <Core/Log.h>
#include <Render/UI/ImGuiExtensions.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <utility>

namespace ed = ax::NodeEditor;

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

// Dock / window titles must stay in sync with DockBuilderDockWindow.
constexpr const char* kWinToolbar = ICON_FA_TOOLBOX " Toolbar";
constexpr const char* kWinViewport = ICON_FA_DISPLAY " Viewport";
constexpr const char* kWinContent = ICON_FA_FOLDER_TREE " Content Browser";
constexpr const char* kWinOutput = ICON_FA_TERMINAL " Output Log";
constexpr const char* kWinBlueprint = ICON_FA_DIAGRAM_PROJECT " Blueprint";
constexpr const char* kWinPlot = ICON_FA_CHART_LINE " Plot";

struct FContentIcon
{
	const char* Glyph = ICON_FA_FILE;
	ImVec4 Color = ImVec4(0.78f, 0.82f, 0.88f, 1.0f);
};

[[nodiscard]] FContentIcon ContentIconForPath(const std::filesystem::path& Path, bool bIsDirectory)
{
	if (bIsDirectory)
	{
		return {ICON_FA_FOLDER, ImVec4(0.95f, 0.78f, 0.35f, 1.0f)};
	}

	std::string Ext = Path.extension().string();
	for (char& Ch : Ext)
	{
		Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(Ch)));
	}

	if (Ext == ".cpp" || Ext == ".h" || Ext == ".hpp" || Ext == ".c" || Ext == ".cc")
	{
		return {ICON_FA_FILE_CODE, ImVec4(0.45f, 0.78f, 0.95f, 1.0f)};
	}
	if (Ext == ".lua" || Ext == ".py" || Ext == ".js" || Ext == ".ts")
	{
		return {ICON_FA_FILE_CODE, ImVec4(0.55f, 0.85f, 0.55f, 1.0f)};
	}
	if (Ext == ".bat" || Ext == ".cmd" || Ext == ".ps1" || Ext == ".sh")
	{
		return {ICON_FA_TERMINAL, ImVec4(0.55f, 0.85f, 0.70f, 1.0f)};
	}
	if (Ext == ".sln" || Ext == ".vcxproj" || Ext == ".cproject" || Ext == ".cmake" || Ext == ".txt")
	{
		return {ICON_FA_FILE_LINES, ImVec4(0.75f, 0.78f, 0.85f, 1.0f)};
	}
	if (Ext == ".png" || Ext == ".jpg" || Ext == ".jpeg" || Ext == ".tga" || Ext == ".bmp")
	{
		return {ICON_FA_FILE_IMAGE, ImVec4(0.90f, 0.55f, 0.85f, 1.0f)};
	}
	if (Ext == ".json" || Ext == ".xml" || Ext == ".ini" || Ext == ".yaml" || Ext == ".yml")
	{
		return {ICON_FA_GEAR, ImVec4(0.70f, 0.75f, 0.82f, 1.0f)};
	}
	if (Ext == ".hlsl" || Ext == ".glsl" || Ext == ".usf" || Ext == ".ush" || Ext == ".shader")
	{
		return {ICON_FA_CUBE, ImVec4(0.70f, 0.60f, 0.95f, 1.0f)};
	}
	return {ICON_FA_FILE, ImVec4(0.78f, 0.82f, 0.88f, 1.0f)};
}

void IdentityMatrix(float* M)
{
	std::memset(M, 0, sizeof(float) * 16);
	M[0] = M[5] = M[10] = M[15] = 1.0f;
}

void LookAtRH(float* Out, float EyeX, float EyeY, float EyeZ, float AtX, float AtY, float AtZ)
{
	float Fx = AtX - EyeX;
	float Fy = AtY - EyeY;
	float Fz = AtZ - EyeZ;
	const float FLen = std::sqrt(Fx * Fx + Fy * Fy + Fz * Fz);
	Fx /= FLen;
	Fy /= FLen;
	Fz /= FLen;
	float Sx = Fy * 0.0f - Fz * 1.0f;
	float Sy = Fz * 0.0f - Fx * 0.0f;
	float Sz = Fx * 1.0f - Fy * 0.0f;
	const float SLen = std::sqrt(Sx * Sx + Sy * Sy + Sz * Sz);
	Sx /= SLen;
	Sy /= SLen;
	Sz /= SLen;
	const float Ux = Sy * Fz - Sz * Fy;
	const float Uy = Sz * Fx - Sx * Fz;
	const float Uz = Sx * Fy - Sy * Fx;
	IdentityMatrix(Out);
	Out[0] = Sx;
	Out[4] = Sy;
	Out[8] = Sz;
	Out[1] = Ux;
	Out[5] = Uy;
	Out[9] = Uz;
	Out[2] = -Fx;
	Out[6] = -Fy;
	Out[10] = -Fz;
	Out[12] = -(Sx * EyeX + Sy * EyeY + Sz * EyeZ);
	Out[13] = -(Ux * EyeX + Uy * EyeY + Uz * EyeZ);
	Out[14] = Fx * EyeX + Fy * EyeY + Fz * EyeZ;
}

void PerspectiveRH(float* Out, float FovYRadians, float Aspect, float ZNear, float ZFar)
{
	IdentityMatrix(Out);
	const float F = 1.0f / std::tan(FovYRadians * 0.5f);
	Out[0] = F / Aspect;
	Out[5] = F;
	Out[10] = ZFar / (ZNear - ZFar);
	Out[11] = -1.0f;
	Out[14] = (ZFar * ZNear) / (ZNear - ZFar);
	Out[15] = 0.0f;
}

} // namespace

FEditorLayer::FEditorLayer()
	: Catty::FLayer("EditorLayer")
{
	IdentityMatrix(ObjectMatrix);
	ObjectMatrix[12] = 0.0f;
	ObjectMatrix[13] = 0.0f;
	ObjectMatrix[14] = 0.0f;
	LookAtRH(ViewMatrix, 5.0f, 4.0f, 5.0f, 0.0f, 0.0f, 0.0f);
	PerspectiveRH(ProjectionMatrix, 45.0f * 3.14159265f / 180.0f, 1.6f, 0.1f, 100.0f);
	GizmoOperation = static_cast<int>(ImGuizmo::TRANSLATE);
}

FEditorLayer::~FEditorLayer()
{
	OnDetach();
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
	AppendOutput("Editor ready. Dock windows freely. CVar: `Name` / `Name Value` / `Dump` / `help`.");

	ed::Config Config;
	Config.SettingsFile = "Config/NodeEditor.json";
	NodeEditorContext = ed::CreateEditor(&Config);
	bBlueprintInited = NodeEditorContext != nullptr;
}

void FEditorLayer::OnDetach()
{
	if (NodeEditorContext)
	{
		ed::DestroyEditor(static_cast<ed::EditorContext*>(NodeEditorContext));
		NodeEditorContext = nullptr;
		bBlueprintInited = false;
	}
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
	DrawDockSpace();
	DrawToolbar();
	DrawViewportPanel();
	DrawContentBrowser();
	DrawOutputPanel(App);
	DrawBlueprintPanel();
	DrawPlotPanel();
	DrawFileDialogs();

	if (bShowDemoWindow)
	{
		ImGui::ShowDemoWindow(&bShowDemoWindow);
	}
	if (bShowImPlotDemo)
	{
		ImPlot::ShowDemoWindow(&bShowImPlotDemo);
	}
}

void FEditorLayer::DrawMainMenuBar(Catty::FApp& App)
{
	if (!ImGui::BeginMainMenuBar())
	{
		return;
	}

	if (ImGui::BeginMenu(ICON_FA_FILE " File"))
	{
		if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN " Open..."))
		{
			IGFD::FileDialogConfig Config;
			Config.path = ContentRoot.string();
			ImGuiFileDialog::Instance()->OpenDialog("EditorOpenDlg", "Open File", ".*", Config);
		}
		if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK " Save As..."))
		{
			IGFD::FileDialogConfig Config;
			Config.path = ContentRoot.string();
			ImGuiFileDialog::Instance()->OpenDialog("EditorSaveDlg", "Save File", ".*", Config);
		}
		ImGui::Separator();
		if (ImGui::MenuItem(ICON_FA_ARROWS_ROTATE " Refresh Content Browser", "F5"))
		{
			RefreshContentListing();
			AppendOutput("Content browser refreshed.");
		}
		ImGui::Separator();
		if (ImGui::MenuItem(ICON_FA_RIGHT_FROM_BRACKET " Exit"))
		{
			App.OnRequestExit();
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu(ICON_FA_WINDOW_MAXIMIZE " Window"))
	{
		ImGui::MenuItem(ICON_FA_SCROLL " Output Auto-Scroll", nullptr, &bAutoScrollOutput);
		ImGui::MenuItem(ICON_FA_TABLE_CELLS " ImGui Demo", nullptr, &bShowDemoWindow);
		ImGui::MenuItem(ICON_FA_CHART_AREA " ImPlot Demo", nullptr, &bShowImPlotDemo);
		if (ImGui::MenuItem(ICON_FA_TABLE_CELLS_LARGE " Reset Dock Layout"))
		{
			bBuildDefaultLayout = true;
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu(ICON_FA_CIRCLE_QUESTION " Help"))
	{
		if (ImGui::MenuItem(ICON_FA_CIRCLE_INFO " CVar help"))
		{
			AppendOutput("Commands: `Dump` | `Name` | `Name Value` | `help`");
		}
		ImGui::EndMenu();
	}

	// Hairline under menu bar so it separates from dock tab strips below.
	{
		const ImVec2 Min = ImGui::GetWindowPos();
		const ImVec2 Size = ImGui::GetWindowSize();
		ImGui::GetWindowDrawList()->AddLine(
			ImVec2(Min.x, Min.y + Size.y - 1.0f),
			ImVec2(Min.x + Size.x, Min.y + Size.y - 1.0f),
			IM_COL32(78, 82, 92, 255),
			1.0f);
	}

	ImGui::EndMainMenuBar();
}

void FEditorLayer::DrawToolbar()
{
	ImGui::Begin(kWinToolbar, nullptr,
		ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);

	const bool bCanPlay = PlayState == EPlayState::Stopped || PlayState == EPlayState::Paused;
	const bool bCanPause = PlayState == EPlayState::Playing;
	const bool bCanStop = PlayState != EPlayState::Stopped;

	ImGui::BeginDisabled(!bCanPlay);
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.42f, 0.28f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.52f, 0.34f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.34f, 0.22f, 1.0f));
	if (ImGui::Button(ICON_FA_PLAY " Play"))
	{
		PlayState = EPlayState::Playing;
		AppendOutput("PIE: Play");
	}
	ImGui::PopStyleColor(3);
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!bCanPause);
	if (ImGui::Button(ICON_FA_PAUSE " Pause"))
	{
		PlayState = EPlayState::Paused;
		AppendOutput("PIE: Pause");
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!bCanStop);
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.42f, 0.18f, 0.18f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.54f, 0.22f, 0.22f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.34f, 0.14f, 0.14f, 1.0f));
	if (ImGui::Button(ICON_FA_STOP " Stop"))
	{
		PlayState = EPlayState::Stopped;
		AppendOutput("PIE: Stop");
	}
	ImGui::PopStyleColor(3);
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextDisabled("|  %s", PlayStateLabel(PlayState));
	ImGui::SameLine();
	ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
	ImGui::SameLine();
	ImGui::RadioButton(ICON_FA_UP_DOWN_LEFT_RIGHT " Translate", &GizmoOperation, static_cast<int>(ImGuizmo::TRANSLATE));
	ImGui::SameLine();
	ImGui::RadioButton(ICON_FA_ROTATE " Rotate", &GizmoOperation, static_cast<int>(ImGuizmo::ROTATE));
	ImGui::SameLine();
	ImGui::RadioButton(ICON_FA_EXPAND " Scale", &GizmoOperation, static_cast<int>(ImGuizmo::SCALE));

	ImGui::End();
}

void FEditorLayer::EnsureDefaultDockLayout(std::uint32_t DockspaceId)
{
	if (!bBuildDefaultLayout)
	{
		return;
	}
	bBuildDefaultLayout = false;

	ImGui::DockBuilderRemoveNode(DockspaceId);
	ImGui::DockBuilderAddNode(DockspaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(DockspaceId, ImGui::GetMainViewport()->WorkSize);

	ImGuiID DockMain = DockspaceId;
	ImGuiID DockLeft = ImGui::DockBuilderSplitNode(DockMain, ImGuiDir_Left, 0.22f, nullptr, &DockMain);
	ImGuiID DockBottom = ImGui::DockBuilderSplitNode(DockMain, ImGuiDir_Down, 0.28f, nullptr, &DockMain);
	ImGuiID DockBottomRight = ImGui::DockBuilderSplitNode(DockBottom, ImGuiDir_Right, 0.55f, nullptr, &DockBottom);
	ImGuiID DockRight = ImGui::DockBuilderSplitNode(DockMain, ImGuiDir_Right, 0.28f, nullptr, &DockMain);

	ImGui::DockBuilderDockWindow(kWinToolbar, DockMain);
	ImGui::DockBuilderDockWindow(kWinViewport, DockMain);
	ImGui::DockBuilderDockWindow(kWinContent, DockLeft);
	ImGui::DockBuilderDockWindow(kWinOutput, DockBottom);
	ImGui::DockBuilderDockWindow(kWinPlot, DockBottomRight);
	ImGui::DockBuilderDockWindow(kWinBlueprint, DockRight);
	ImGui::DockBuilderFinish(DockspaceId);
}

void FEditorLayer::DrawDockSpace()
{
	ImGuiViewport* Viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(Viewport->WorkPos);
	ImGui::SetNextWindowSize(Viewport->WorkSize);
	ImGui::SetNextWindowViewport(Viewport->ID);

	// Full-viewport chassis; WindowPadding insets DockSpace so panels sit inside a
	// chrome ring instead of flush against the OS/client edge.
	const float OuterPad = 8.0f;
	const ImVec4 DockChassis = ImVec4(14.0f / 255.0f, 14.0f / 255.0f, 16.0f / 255.0f, 1.0f);
	ImGuiWindowFlags HostFlags =
		ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoBringToFrontOnFocus
		| ImGuiWindowFlags_NoNavFocus;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ImGui::GetStyle().WindowRounding);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(OuterPad, OuterPad));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, DockChassis);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, DockChassis);
	ImGui::Begin("##EditorDockHost", nullptr, HostFlags);
	ImGui::PopStyleVar(3);

	// DockNodeUpdate copies Style.Colors[Border] into Separator — keep it clear
	// here so gutters stay chassis-colored; floating windows keep visible Border.
	ImGuiStyle& Style = ImGui::GetStyle();
	const ImVec4 BackupBorder = Style.Colors[ImGuiCol_Border];
	Style.Colors[ImGuiCol_Border] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

	const ImGuiID DockspaceId = ImGui::GetID("CattyEditorDockspaceChassis");
	EnsureDefaultDockLayout(DockspaceId);
	ImGui::DockSpace(DockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

	Style.Colors[ImGuiCol_Border] = BackupBorder;
	ImGui::End();
	ImGui::PopStyleColor(2);
}

void FEditorLayer::DrawViewportPanel()
{
	ImGui::Begin(kWinViewport);
	const ImVec2 Canvas = ImGui::GetContentRegionAvail();
	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->AddRectFilled(Origin, ImVec2(Origin.x + Canvas.x, Origin.y + Canvas.y), IM_COL32(8, 9, 11, 255));
	DrawList->AddText(
		ImVec2(Origin.x + 12.0f, Origin.y + 12.0f),
		IM_COL32(110, 116, 128, 255),
		ICON_FA_DISPLAY "  Main Viewport + ImGuizmo");

	ImGuizmo::SetOrthographic(false);
	ImGuizmo::SetDrawlist();
	ImGuizmo::SetRect(Origin.x, Origin.y, Canvas.x, Canvas.y);
	if (Canvas.x > 1.0f && Canvas.y > 1.0f)
	{
		PerspectiveRH(ProjectionMatrix, 45.0f * 3.14159265f / 180.0f, Canvas.x / Canvas.y, 0.1f, 100.0f);
		ImGuizmo::DrawGrid(ViewMatrix, ProjectionMatrix, ObjectMatrix, 10.0f);
		ImGuizmo::Manipulate(
			ViewMatrix,
			ProjectionMatrix,
			static_cast<ImGuizmo::OPERATION>(GizmoOperation),
			ImGuizmo::LOCAL,
			ObjectMatrix);
	}
	ImGui::Dummy(Canvas);
	ImGui::End();
}

void FEditorLayer::DrawContentBrowser()
{
	ImGui::Begin(kWinContent);
	if (ImGui::Button(ICON_FA_ARROW_UP " Up"))
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
	if (ImGui::Button(ICON_FA_ARROWS_ROTATE " Refresh"))
	{
		RefreshContentListing();
	}
	ImGui::SameLine();
	ImGui::TextDisabled(ICON_FA_FOLDER_OPEN);
	ImGui::SameLine();
	ImGui::TextWrapped("%s", CurrentFolder.string().c_str());
	ImGui::Separator();

	// List area uses deepest chassis bg (same as dock gutters), not panel sheet gray.
	const ImVec4 DeepBg = ImVec4(14.0f / 255.0f, 14.0f / 255.0f, 16.0f / 255.0f, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, DeepBg);
	ImGui::BeginChild("##ContentList", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
	for (const std::filesystem::path& Folder : FolderEntries)
	{
		const FContentIcon Icon = ContentIconForPath(Folder, true);
		ImGui::PushStyleColor(ImGuiCol_Text, Icon.Color);
		const std::string Label = std::string(Icon.Glyph) + "  " + Folder.filename().string();
		if (ImGui::Selectable(Label.c_str(), SelectedEntry == Folder.string(), ImGuiSelectableFlags_AllowDoubleClick))
		{
			SelectedEntry = Folder.string();
			if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				CurrentFolder = Folder;
				RefreshContentListing();
			}
		}
		ImGui::PopStyleColor();
	}
	for (const std::filesystem::path& File : FileEntries)
	{
		const FContentIcon Icon = ContentIconForPath(File, false);
		ImGui::PushStyleColor(ImGuiCol_Text, Icon.Color);
		const std::string Label = std::string(Icon.Glyph) + "  " + File.filename().string();
		if (ImGui::Selectable(Label.c_str(), SelectedEntry == File.string()))
		{
			SelectedEntry = File.string();
		}
		ImGui::PopStyleColor();
	}
	ImGui::EndChild();
	ImGui::PopStyleColor();
	ImGui::End();
}

void FEditorLayer::DrawOutputPanel(Catty::FApp& App)
{
	ImGui::Begin(kWinOutput);
	const float Footer = ImGui::GetFrameHeightWithSpacing() + 4.0f;
	ImGui::BeginChild("##OutputScroll", ImVec2(0.0f, -Footer), ImGuiChildFlags_AlwaysUseWindowPadding);
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
	if (ImGui::InputText(
			"##ConsoleInput",
			ConsoleInput,
			IM_ARRAYSIZE(ConsoleInput),
			ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll))
	{
		const std::string Line = ConsoleInput;
		ConsoleInput[0] = '\0';
		ExecuteConsoleLine(App, Line);
		ImGui::SetKeyboardFocusHere(-1);
	}
	ImGui::End();
}

void FEditorLayer::DrawBlueprintPanel()
{
	ImGui::Begin(kWinBlueprint);
	if (!bBlueprintInited || !NodeEditorContext)
	{
		ImGui::TextDisabled("Node editor context unavailable.");
		ImGui::End();
		return;
	}

	ed::SetCurrentEditor(static_cast<ed::EditorContext*>(NodeEditorContext));
	// Match Catty panel WindowBg so the canvas does not read as a lighter child sheet.
	{
		ed::Style& NodeStyle = ed::GetStyle();
		const ImVec4 PanelBg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
		NodeStyle.Colors[ed::StyleColor_Bg] = PanelBg;
		NodeStyle.Colors[ed::StyleColor_Grid] = ImVec4(1.0f, 1.0f, 1.0f, 0.04f);
	}
	ed::Begin("BlueprintCanvas");

	ed::BeginNode(ed::NodeId(1));
	ImGui::Text("Event BeginPlay");
	ed::BeginPin(ed::PinId(11), ed::PinKind::Output);
	ImGui::Text("exec " ICON_FA_ARROW_RIGHT);
	ed::EndPin();
	ed::EndNode();

	ed::BeginNode(ed::NodeId(2));
	ImGui::Text("Print String");
	ed::BeginPin(ed::PinId(21), ed::PinKind::Input);
	ImGui::Text(ICON_FA_ARROW_LEFT " exec");
	ed::EndPin();
	ed::EndNode();

	if (ed::Link(ed::LinkId(100), ed::PinId(11), ed::PinId(21)))
	{
	}

	ed::End();
	ed::SetCurrentEditor(nullptr);
	ImGui::End();
}

void FEditorLayer::DrawPlotPanel()
{
	ImGui::Begin(kWinPlot);
	static float Values[90] = {};
	static int Offset = 0;
	Values[Offset] = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 3.0f);
	Offset = (Offset + 1) % IM_ARRAYSIZE(Values);

	if (ImPlot::BeginPlot("Signal", ImVec2(-1, -1)))
	{
		ImPlot::PlotLine("sin", Values, IM_ARRAYSIZE(Values), 1.0, 0, ImPlotLineFlags_None, Offset);
		ImPlot::EndPlot();
	}
	ImGui::End();
}

void FEditorLayer::DrawFileDialogs()
{
	const ImVec2 MaxSize = ImVec2(900.0f, 600.0f);
	const ImVec2 MinSize = ImVec2(500.0f, 300.0f);
	if (ImGuiFileDialog::Instance()->Display("EditorOpenDlg", ImGuiWindowFlags_NoCollapse, MinSize, MaxSize))
	{
		if (ImGuiFileDialog::Instance()->IsOk())
		{
			AppendOutput(std::string("Open: ") + ImGuiFileDialog::Instance()->GetFilePathName());
		}
		ImGuiFileDialog::Instance()->Close();
	}
	if (ImGuiFileDialog::Instance()->Display("EditorSaveDlg", ImGuiWindowFlags_NoCollapse, MinSize, MaxSize))
	{
		if (ImGuiFileDialog::Instance()->IsOk())
		{
			AppendOutput(std::string("Save: ") + ImGuiFileDialog::Instance()->GetFilePathName());
		}
		ImGuiFileDialog::Instance()->Close();
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
		AppendOutput("  Dump | <Name> | <Name> <Value>");
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
		return;
	}
	AppendOutput(CVarName + " = " + Variable->GetString());
}
