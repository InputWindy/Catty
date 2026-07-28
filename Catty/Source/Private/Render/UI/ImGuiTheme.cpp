#include <Render/UI/ImGuiTheme.h>

#include <imgui.h>

namespace Catty
{

namespace
{

[[nodiscard]] ImVec4 Rgba(int R, int G, int B, float A = 1.0f)
{
	return ImVec4(
		static_cast<float>(R) / 255.0f,
		static_cast<float>(G) / 255.0f,
		static_cast<float>(B) / 255.0f,
		A);
}

} // namespace

void ApplyCattyNightTheme()
{
	ImGuiStyle& Style = ImGui::GetStyle();

	Style.Alpha = 1.0f;
	Style.DisabledAlpha = 0.38f;
	Style.WindowPadding = ImVec2(8.0f, 8.0f);
	Style.WindowRounding = 0.0f;
	Style.WindowBorderSize = 0.0f;
	Style.WindowMinSize = ImVec2(96.0f, 48.0f);
	Style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
	Style.WindowMenuButtonPosition = ImGuiDir_None;
	Style.ChildRounding = 0.0f;
	// Must be > 0: ImGui zeroes Child WindowPadding when border size is 0.
	Style.ChildBorderSize = 1.0f;
	Style.PopupRounding = 2.0f;
	Style.PopupBorderSize = 1.0f;
	Style.FramePadding = ImVec2(8.0f, 5.0f);
	Style.FrameRounding = 2.0f;
	Style.FrameBorderSize = 1.0f;
	Style.ItemSpacing = ImVec2(6.0f, 5.0f);
	Style.ItemInnerSpacing = ImVec2(5.0f, 3.0f);
	Style.CellPadding = ImVec2(5.0f, 3.0f);
	Style.IndentSpacing = 16.0f;
	Style.ColumnsMinSpacing = 4.0f;
	Style.ScrollbarSize = 12.0f;
	Style.ScrollbarRounding = 2.0f;
	Style.GrabMinSize = 10.0f;
	Style.GrabRounding = 2.0f;
	Style.TabRounding = 0.0f;
	Style.TabBorderSize = 0.0f;
	Style.TabBarBorderSize = 0.0f; // hide tab-bar bottom separator (uses TabSelected color)
	Style.TabBarOverlineSize = 2.0f;
	Style.TabCloseButtonMinWidthSelected = -1.0f;
	Style.TabCloseButtonMinWidthUnselected = 0.0f;
	Style.ColorButtonPosition = ImGuiDir_Right;
	Style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
	Style.SelectableTextAlign = ImVec2(0.0f, 0.0f);
	Style.SeparatorTextBorderSize = 1.0f;
	Style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
	Style.SeparatorTextPadding = ImVec2(10.0f, 2.0f);
	Style.DockingSeparatorSize = 4.0f; // keep hit area; fill is fully transparent via Border

	// Top chrome hierarchy:
	//   MenuBar → recessed Tab well → selected tab flushes with Panel (raised out of the well)
	const ImVec4 MenuBar = Rgba(12, 12, 14);
	const ImVec4 TabWell = Rgba(14, 14, 16);       // sunken strip behind unselected tabs / dock splitters
	const ImVec4 TabIdle = Rgba(20, 21, 24);       // unselected tab face (still in the well)
	const ImVec4 Panel = Rgba(38, 39, 43);         // content + selected tab (same plane)
	const ImVec4 Well = Rgba(26, 27, 30);
	const ImVec4 Raised = Rgba(52, 54, 60);
	const ImVec4 Hover = Rgba(66, 70, 78);
	const ImVec4 Pressed = Rgba(30, 31, 35);
	const ImVec4 Edge = Rgba(78, 82, 92);
	const ImVec4 EdgeStrong = Rgba(96, 100, 112);
	const ImVec4 EdgeSoft = Rgba(48, 50, 56);
	const ImVec4 Text = Rgba(236, 237, 240);
	const ImVec4 TextMuted = Rgba(124, 128, 138);
	const ImVec4 Accent = Rgba(70, 148, 235);
	const ImVec4 AccentSoft = Rgba(70, 148, 235, 0.30f);
	const ImVec4 AccentHover = Rgba(100, 170, 250);
	const ImVec4 AccentDim = Rgba(70, 148, 235, 0.50f);

	ImVec4* Colors = Style.Colors;
	Colors[ImGuiCol_Text] = Text;
	Colors[ImGuiCol_TextDisabled] = TextMuted;
	Colors[ImGuiCol_WindowBg] = Panel;
	Colors[ImGuiCol_ChildBg] = Well;
	Colors[ImGuiCol_PopupBg] = Rgba(28, 29, 33, 0.98f);
	// Dock splitters: ImGui remaps Separator → Border, then SplitterBehavior
	// paints WindowBg under that. Border stays fully transparent; the visible
	// gutter is WindowBg of the dock host (kept as TabWell while DockSpace runs).
	Colors[ImGuiCol_Border] = Rgba(0, 0, 0, 0.0f);
	Colors[ImGuiCol_BorderShadow] = Rgba(0, 0, 0, 0.0f);
	Colors[ImGuiCol_FrameBg] = Well;
	Colors[ImGuiCol_FrameBgHovered] = Raised;
	Colors[ImGuiCol_FrameBgActive] = Pressed;
	// Title strip = recessed tab well (docked tab bars read as inset).
	Colors[ImGuiCol_TitleBg] = TabWell;
	Colors[ImGuiCol_TitleBgActive] = TabWell;
	Colors[ImGuiCol_TitleBgCollapsed] = TabWell;
	Colors[ImGuiCol_MenuBarBg] = MenuBar;
	Colors[ImGuiCol_ScrollbarBg] = TabWell;
	Colors[ImGuiCol_ScrollbarGrab] = Raised;
	Colors[ImGuiCol_ScrollbarGrabHovered] = Hover;
	Colors[ImGuiCol_ScrollbarGrabActive] = Accent;
	Colors[ImGuiCol_CheckMark] = AccentHover;
	Colors[ImGuiCol_SliderGrab] = Accent;
	Colors[ImGuiCol_SliderGrabActive] = AccentHover;
	Colors[ImGuiCol_Button] = Raised;
	Colors[ImGuiCol_ButtonHovered] = Hover;
	Colors[ImGuiCol_ButtonActive] = Pressed;
	Colors[ImGuiCol_Header] = Rgba(52, 54, 60, 0.75f);
	Colors[ImGuiCol_HeaderHovered] = AccentSoft;
	Colors[ImGuiCol_HeaderActive] = AccentDim;
	// Invisible dock/window separators (dock path uses Border; keep Separator in sync).
	Colors[ImGuiCol_Separator] = Rgba(0, 0, 0, 0.0f);
	Colors[ImGuiCol_SeparatorHovered] = AccentSoft;
	Colors[ImGuiCol_SeparatorActive] = Accent;
	Colors[ImGuiCol_ResizeGrip] = Rgba(255, 255, 255, 0.12f);
	Colors[ImGuiCol_ResizeGripHovered] = AccentSoft;
	Colors[ImGuiCol_ResizeGripActive] = Accent;
	Colors[ImGuiCol_Tab] = TabIdle;
	Colors[ImGuiCol_TabHovered] = Hover;
	Colors[ImGuiCol_TabSelected] = Panel;
	Colors[ImGuiCol_TabSelectedOverline] = Accent;
	Colors[ImGuiCol_TabDimmed] = TabWell;
	Colors[ImGuiCol_TabDimmedSelected] = Panel;
	Colors[ImGuiCol_TabDimmedSelectedOverline] = Rgba(70, 148, 235, 0.45f);
	Colors[ImGuiCol_DockingPreview] = AccentSoft;
	Colors[ImGuiCol_DockingEmptyBg] = TabWell;
	Colors[ImGuiCol_PlotLines] = Rgba(150, 165, 190);
	Colors[ImGuiCol_PlotLinesHovered] = AccentHover;
	Colors[ImGuiCol_PlotHistogram] = Accent;
	Colors[ImGuiCol_PlotHistogramHovered] = AccentHover;
	Colors[ImGuiCol_TableHeaderBg] = TabWell;
	Colors[ImGuiCol_TableBorderStrong] = EdgeStrong;
	Colors[ImGuiCol_TableBorderLight] = EdgeSoft;
	Colors[ImGuiCol_TableRowBg] = Rgba(0, 0, 0, 0.0f);
	Colors[ImGuiCol_TableRowBgAlt] = Rgba(255, 255, 255, 0.025f);
	Colors[ImGuiCol_TextLink] = AccentHover;
	Colors[ImGuiCol_TextSelectedBg] = AccentSoft;
	Colors[ImGuiCol_DragDropTarget] = Accent;
	Colors[ImGuiCol_NavCursor] = Accent;
	Colors[ImGuiCol_NavWindowingHighlight] = Rgba(255, 255, 255, 0.55f);
	Colors[ImGuiCol_NavWindowingDimBg] = Rgba(0, 0, 0, 0.70f);
	Colors[ImGuiCol_ModalWindowDimBg] = Rgba(0, 0, 0, 0.78f);
}

} // namespace Catty
