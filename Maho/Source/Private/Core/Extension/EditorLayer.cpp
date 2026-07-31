#include <Core/Extension/EditorLayer.h>

#include <Core/Application/App.h>
#include <Core/System/ConfigFile.h>
#include <Core/System/Console.h>
#include <Core/Editor/AgentChatClient.h>
#include <Core/Editor/EditorUIRegistry.h>
#include <Core/System/Log.h>
#include <Core/System/Paths.h>
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

#if defined(_WIN32)
#	ifndef NOMINMAX
#		define NOMINMAX
#	endif
#	include <Windows.h>
#endif
namespace ed = ax::NodeEditor;

namespace Maho
{

// Set to 1 to restore previous demo widgets inside editor panels.
#define MAHO_EDITOR_DEMO_CONTENT 0
// Output / Blueprint / Plot / file dialogs — keep code, hide from shell for now.
#define MAHO_EDITOR_EXTRA_PANELS 0
// Temporary: only ShowDemoWindow on Update (isolate ImGui::Render crash).
#define MAHO_EDITOR_DEMO_ONLY 0

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

#if MAHO_EDITOR_DEMO_CONTENT
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
#endif

/** Hide dock-node title-bar close (far-right X); tab close still works when p_open != nullptr. */
void ApplyDockTabOnlyCloseClass()
{
	ImGuiWindowClass Class;
	Class.DockNodeFlagsOverrideSet = static_cast<ImGuiDockNodeFlags>(
		static_cast<int>(ImGuiDockNodeFlags_NoWindowMenuButton)
		| static_cast<int>(ImGuiDockNodeFlags_NoCloseButton));
	ImGui::SetNextWindowClass(&Class);
}

[[nodiscard]] bool BeginEditorDockPanel(const char* Title, bool* bOpen, ImGuiWindowFlags Flags = 0)
{
	ApplyDockTabOnlyCloseClass();
	return ImGui::Begin(Title, bOpen, Flags);
}

// Dock / window titles must stay in sync with DockBuilderDockWindow.
constexpr const char* kWinMainViewport = ICON_FA_MAP "  MyGame";
constexpr const char* kWinContent = ICON_FA_FOLDER_TREE " Content Browser";
constexpr const char* kWinOutput = ICON_FA_TERMINAL " Output Log";
constexpr const char* kWinAgent = ICON_FA_COMMENTS " Agent";
constexpr const char* kWinBlueprint = ICON_FA_DIAGRAM_PROJECT " Blueprint";
constexpr const char* kWinSequenceGraph = ICON_FA_SHARE_NODES " Sequence Graph";
constexpr const char* kWinPlot = ICON_FA_CHART_LINE " Plot";
constexpr const char* kWinTransientDetails = ICON_FA_SLIDERS "  Temporary Details";
constexpr const char* kModalBusyTitle = "Busy";

[[nodiscard]] const char* EngineStageLabel(EEngineStage Stage)
{
	switch (Stage)
	{
	case EEngineStage::PreInit: return "PreInit";
	case EEngineStage::Init: return "Init";
	case EEngineStage::PostInit: return "PostInit";
	case EEngineStage::Attach: return "Attach";
	case EEngineStage::BeginFrame: return "BeginFrame";
	case EEngineStage::ProcessInput: return "ProcessInput";
	case EEngineStage::FixedUpdate: return "FixedUpdate";
	case EEngineStage::Update: return "Update";
	case EEngineStage::LateUpdate: return "LateUpdate";
	case EEngineStage::EndFrame: return "EndFrame";
	case EEngineStage::PreRender: return "PreRender";
	case EEngineStage::Render: return "Render";
	case EEngineStage::PostRender: return "PostRender";
	case EEngineStage::Detach: return "Detach";
	case EEngineStage::PrepareExit: return "PrepareExit";
	case EEngineStage::Shutdown: return "Shutdown";
	default: return "?";
	}
}

[[nodiscard]] const char* AppStateLabel(EAppState State)
{
	switch (State)
	{
	case EAppState::Stopped: return "Stopped";
	case EAppState::Running: return "Running";
	case EAppState::WaitForExit: return "WaitForExit";
	default: return "?";
	}
}

// Stable IDs for Sequence Graph canvas (must not collide across modes).
namespace SeqGraphIds
{
	constexpr int ExtNodeBase = 1000;
	constexpr int ExtInPinBase = 11000;
	constexpr int ExtOutPinBase = 12000;
	constexpr int LifeNodeBase = 5000;
	constexpr int LifeOutPinBase = 5100;
	constexpr int LifeInPinBase = 5200;
}

[[nodiscard]] const char* ExtensionPriorityLabel(EExtensionPriority Priority)
{
	switch (Priority)
	{
	case EExtensionPriority::System: return "System";
	case EExtensionPriority::Layer: return "Layer";
	case EExtensionPriority::Overlay: return "Overlay";
	default: return "?";
	}
}

[[nodiscard]] ImVec4 ExtensionPriorityColor(EExtensionPriority Priority)
{
	switch (Priority)
	{
	case EExtensionPriority::System:
		return ImVec4(0.55f, 0.78f, 1.0f, 1.0f);
	case EExtensionPriority::Layer:
		return ImVec4(0.70f, 0.95f, 0.55f, 1.0f);
	case EExtensionPriority::Overlay:
		return ImVec4(0.95f, 0.75f, 0.45f, 1.0f);
	default:
		return ImVec4(0.80f, 0.80f, 0.80f, 1.0f);
	}
}

[[nodiscard]] int FindExtensionIndex(
	const std::vector<IEngineExtension*>& Extensions,
	IEngineExtension* Target)
{
	for (std::size_t Index = 0; Index < Extensions.size(); ++Index)
	{
		if (Extensions[Index] == Target)
		{
			return static_cast<int>(Index);
		}
	}
	return -1;
}


[[nodiscard]] ImVec4 OutputColorForLevel(spdlog::level::level_enum Level)
{
	switch (Level)
	{
	case spdlog::level::trace:
		return ImVec4(0.50f, 0.52f, 0.56f, 1.0f);
	case spdlog::level::debug:
		return ImVec4(0.62f, 0.72f, 0.85f, 1.0f);
	case spdlog::level::info:
		return ImVec4(0.88f, 0.90f, 0.92f, 1.0f);
	case spdlog::level::warn:
		return ImVec4(0.95f, 0.80f, 0.28f, 1.0f);
	case spdlog::level::err:
		return ImVec4(0.95f, 0.42f, 0.38f, 1.0f);
	case spdlog::level::critical:
		return ImVec4(1.0f, 0.28f, 0.40f, 1.0f);
	default:
		return ImVec4(0.88f, 0.90f, 0.92f, 1.0f);
	}
}

[[nodiscard]] ImVec4 AgentColorForRole(EAgentChatRole Role)
{
	switch (Role)
	{
	case EAgentChatRole::User:
		return ImVec4(0.55f, 0.82f, 1.0f, 1.0f);
	case EAgentChatRole::Assistant:
		return ImVec4(0.88f, 0.90f, 0.92f, 1.0f);
	default:
		return ImVec4(0.70f, 0.72f, 0.55f, 1.0f);
	}
}

[[nodiscard]] const char* AgentRoleLabel(EAgentChatRole Role)
{
	switch (Role)
	{
	case EAgentChatRole::User:
		return "You";
	case EAgentChatRole::Assistant:
		return "Agent";
	default:
		return "System";
	}
}

#if MAHO_EDITOR_DEMO_CONTENT
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
#endif

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
	: FLayer("EditorLayer")
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
	UnmountEditor();
}

void FEditorLayer::MountEditor()
{
	if (bEditorMounted)
	{
		return;
	}
	bEditorMounted = true;

	EnsureContentMounts();
	SelectContentFolder("/Game");
	AppendOutput("Output Log ready. Commands: `Dump` | `<Name>` | `<Name> <Value>` | `help`.");
	StartAgentChat();

	{
		ed::Config Config;
		Config.SettingsFile = "Config/SequenceGraphNodeEditor.json";
		SequenceGraphEditorContext = ed::CreateEditor(&Config);
		bSequenceGraphEditorInited = SequenceGraphEditorContext != nullptr;
		bSequenceGraphLayoutApplied = false;
	}

#if MAHO_EDITOR_DEMO_CONTENT
	ed::Config Config;
	Config.SettingsFile = "Config/NodeEditor.json";
	NodeEditorContext = ed::CreateEditor(&Config);
	bBlueprintInited = NodeEditorContext != nullptr;
#endif

	RegisterBuiltinUIContributions();
	RegisterDummyUIContributions();
}

void FEditorLayer::UnmountEditor()
{
	if (!bEditorMounted)
	{
		return;
	}
	bEditorMounted = false;

	UIRegistry.Clear();

	if (AgentChat)
	{
		AgentChat->Stop();
		AgentChat.reset();
	}
	if (SequenceGraphEditorContext)
	{
		ed::DestroyEditor(static_cast<ed::EditorContext*>(SequenceGraphEditorContext));
		SequenceGraphEditorContext = nullptr;
		bSequenceGraphEditorInited = false;
		bSequenceGraphLayoutApplied = false;
	}
	if (NodeEditorContext)
	{
		ed::DestroyEditor(static_cast<ed::EditorContext*>(NodeEditorContext));
		NodeEditorContext = nullptr;
		bBlueprintInited = false;
	}
}

FEditorUIDrawContext FEditorLayer::MakeUIDrawContext(FApp& App)
{
	FEditorUIDrawContext Ctx;
	Ctx.App = &App;
	Ctx.Editor = this;
	Ctx.Registry = &UIRegistry;
	return Ctx;
}

void FEditorLayer::RegisterBuiltinUIContributions()
{
	UIRegistry.Clear();

	const FEditorUICatalog CatFileOps{ "FileOps", 10 };
	const FEditorUICatalog CatTransform{ "Transform", 20 };
	const FEditorUICatalog CatPlay{ "Play", 30 };
	const FEditorUICatalog CatBrowser{ "Browser", 10 };
	const FEditorUICatalog CatLog{ "Log", 20 };
	const FEditorUICatalog CatTools{ "Tools", 30 };
	const FEditorUICatalog CatDetails{ "Details", 40 };
	const FEditorUICatalog CatSystem{ "System", 10 };
	const FEditorUICatalog CatDebug{ "Debug", 100 };
	const FEditorUICatalog CatWindow{ "Window", 20 };
	const FEditorUICatalog CatHelp{ "Help", 30 };

	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarPrimary,
		CatFileOps,
		"builtin.tb1.save",
		0,
		[](FEditorUIDrawContext&)
		{
			ImGui::Button(ICON_FA_FLOPPY_DISK "##Tb1Save");
		} });
	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarPrimary,
		CatFileOps,
		"builtin.tb1.open",
		1,
		[](FEditorUIDrawContext&)
		{
			ImGui::Button(ICON_FA_FOLDER_OPEN "##Tb1Open");
		} });
	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarPrimary,
		CatFileOps,
		"builtin.tb1.search",
		2,
		[](FEditorUIDrawContext&)
		{
			ImGui::Button(ICON_FA_MAGNIFYING_GLASS "##Tb1Search");
		} });

	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarSecondary,
		CatTransform,
		"builtin.tb2.select",
		0,
		[this](FEditorUIDrawContext& Ctx)
		{
			const ImVec2 BtnSize(Ctx.ToolbarButtonSize, Ctx.ToolbarButtonSize);
			if (ImGui::Button(ICON_FA_ARROW_POINTER "##Tb2Select", BtnSize))
			{
				ViewportTool = EViewportTool::Select;
			}
		} });
	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarSecondary,
		CatTransform,
		"builtin.tb2.translate",
		1,
		[this](FEditorUIDrawContext& Ctx)
		{
			const ImVec2 BtnSize(Ctx.ToolbarButtonSize, Ctx.ToolbarButtonSize);
			if (ImGui::Button(ICON_FA_UP_DOWN_LEFT_RIGHT "##Tb2Translate", BtnSize))
			{
				ViewportTool = EViewportTool::Translate;
				GizmoOperation = static_cast<int>(ImGuizmo::TRANSLATE);
			}
		} });
	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarSecondary,
		CatTransform,
		"builtin.tb2.rotate",
		2,
		[this](FEditorUIDrawContext& Ctx)
		{
			const ImVec2 BtnSize(Ctx.ToolbarButtonSize, Ctx.ToolbarButtonSize);
			if (ImGui::Button(ICON_FA_ARROW_ROTATE_RIGHT "##Tb2Rotate", BtnSize))
			{
				ViewportTool = EViewportTool::Rotate;
				GizmoOperation = static_cast<int>(ImGuizmo::ROTATE);
			}
		} });
	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarSecondary,
		CatTransform,
		"builtin.tb2.scale",
		3,
		[this](FEditorUIDrawContext& Ctx)
		{
			const ImVec2 BtnSize(Ctx.ToolbarButtonSize, Ctx.ToolbarButtonSize);
			if (ImGui::Button(ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER "##Tb2Scale", BtnSize))
			{
				ViewportTool = EViewportTool::Scale;
				GizmoOperation = static_cast<int>(ImGuizmo::SCALE);
			}
		} });

	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarSecondary,
		CatPlay,
		"builtin.tb2.play",
		0,
		[this](FEditorUIDrawContext& Ctx)
		{
			const ImVec2 BtnSize(Ctx.ToolbarButtonSize, Ctx.ToolbarButtonSize);
			const bool bCanPlay = PlayState == EPlayState::Stopped || PlayState == EPlayState::Paused;
			ImGui::BeginDisabled(!bCanPlay);
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.86f, 0.42f, 1.0f));
			if (ImGui::Button(ICON_FA_PLAY "##Tb2Play", BtnSize))
			{
				PlayState = EPlayState::Playing;
				AppendOutput("PIE: Play");
			}
			ImGui::PopStyleColor();
			ImGui::EndDisabled();
		} });
	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarSecondary,
		CatPlay,
		"builtin.tb2.step",
		1,
		[this](FEditorUIDrawContext& Ctx)
		{
			const ImVec2 BtnSize(Ctx.ToolbarButtonSize, Ctx.ToolbarButtonSize);
			const bool bCanStep = PlayState != EPlayState::Stopped;
			ImGui::BeginDisabled(!bCanStep);
			if (ImGui::Button(ICON_FA_FORWARD_STEP "##Tb2Step", BtnSize))
			{
				PlayState = EPlayState::Paused;
				AppendOutput("PIE: Step / Pause");
			}
			ImGui::EndDisabled();
		} });
	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarSecondary,
		CatPlay,
		"builtin.tb2.stop",
		2,
		[this](FEditorUIDrawContext& Ctx)
		{
			const ImVec2 BtnSize(Ctx.ToolbarButtonSize, Ctx.ToolbarButtonSize);
			const bool bCanStop = PlayState != EPlayState::Stopped;
			ImGui::BeginDisabled(!bCanStop);
			if (ImGui::Button(ICON_FA_STOP "##Tb2Stop", BtnSize))
			{
				PlayState = EPlayState::Stopped;
				AppendOutput("PIE: Stop");
			}
			ImGui::EndDisabled();
		} });

	UIRegistry.RegisterDockPanel({
		CatBrowser,
		"dock.content",
		kWinContent,
		&bShowContentBrowser,
		true,
		false,
		0,
		[this](FEditorUIDrawContext&)
		{
			DrawContentBrowser();
		} });
	UIRegistry.RegisterDockPanel({
		CatLog,
		"dock.output",
		kWinOutput,
		&bShowOutputPanel,
		true,
		false,
		0,
		[this](FEditorUIDrawContext& Ctx)
		{
			if (Ctx.App)
			{
				DrawOutputPanel(*Ctx.App);
			}
		} });
	UIRegistry.RegisterDockPanel({
		CatTools,
		"dock.agent",
		kWinAgent,
		&bShowAgentPanel,
		true,
		false,
		0,
		[this](FEditorUIDrawContext&)
		{
			DrawAgentPanel();
		} });
	UIRegistry.RegisterDockPanel({
		CatTools,
		"dock.sequence",
		kWinSequenceGraph,
		&bShowSequenceGraphPanel,
		true,
		false,
		1,
		[this](FEditorUIDrawContext& Ctx)
		{
			if (Ctx.App)
			{
				DrawSequenceGraphPanel(*Ctx.App);
			}
		} });
#if MAHO_EDITOR_EXTRA_PANELS
	UIRegistry.RegisterDockPanel({
		CatTools,
		"dock.blueprint",
		kWinBlueprint,
		&bShowBlueprintPanel,
		true,
		false,
		2,
		[this](FEditorUIDrawContext&)
		{
			DrawBlueprintPanel();
		} });
	UIRegistry.RegisterDockPanel({
		CatTools,
		"dock.plot",
		kWinPlot,
		&bShowPlotPanel,
		true,
		false,
		3,
		[this](FEditorUIDrawContext&)
		{
			DrawPlotPanel();
		} });
#endif
	UIRegistry.RegisterDockPanel({
		CatDetails,
		"dock.temp_details",
		kWinTransientDetails,
		&bShowTransientDetails,
		false,
		true,
		0,
		[this](FEditorUIDrawContext&)
		{
			DrawTransientDetailsPanel();
		} });

	UIRegistry.RegisterModal({
		CatSystem,
		"modal.busy",
		kModalBusyTitle,
		0,
		[this](FEditorUIDrawContext& Ctx)
		{
			ImGui::TextUnformatted("Blocking work in progress…");
			ImGui::Spacing();
			if (ImGui::Button("Close", ImVec2(120.0f, 0.0f)) && Ctx.Registry)
			{
				Ctx.Registry->CloseModal("modal.busy");
			}
		} });

	UIRegistry.RegisterMenuItem({
		CatWindow,
		"menu.window.output_autoscroll",
		ICON_FA_SCROLL "  Output Auto-Scroll",
		10,
		[this](FEditorUIDrawContext&)
		{
			ImGui::MenuItem(ICON_FA_SCROLL "  Output Auto-Scroll", nullptr, &bAutoScrollOutput);
		} });
	UIRegistry.RegisterMenuItem({
		CatWindow,
		"menu.window.agent_autoscroll",
		ICON_FA_SCROLL "  Agent Auto-Scroll",
		11,
		[this](FEditorUIDrawContext&)
		{
			ImGui::MenuItem(ICON_FA_SCROLL "  Agent Auto-Scroll", nullptr, &bAutoScrollAgent);
		} });
	UIRegistry.RegisterMenuItem({
		CatWindow,
		"menu.window.reset_dock",
		ICON_FA_TABLE_CELLS_LARGE "  Reset Dock Layout",
		20,
		[this](FEditorUIDrawContext&)
		{
			if (ImGui::MenuItem(ICON_FA_TABLE_CELLS_LARGE "  Reset Dock Layout"))
			{
				bBuildDefaultLayout = true;
			}
		} });

	UIRegistry.RegisterMenuItem({
		CatDebug,
		"menu.debug.toggle_dummy",
		"Show Dummy UI",
		0,
		[this](FEditorUIDrawContext&)
		{
			ImGui::MenuItem("Show Dummy UI", nullptr, &bShowDummyUI);
		} });
	UIRegistry.RegisterMenuItem({
		CatDebug,
		"menu.debug.open_details",
		"Open Temporary Details",
		1,
		[this](FEditorUIDrawContext& Ctx)
		{
			if (ImGui::MenuItem("Open Temporary Details") && Ctx.Registry)
			{
				Ctx.Registry->OpenDockPanel("dock.temp_details");
			}
		} });
	UIRegistry.RegisterMenuItem({
		CatDebug,
		"menu.debug.open_busy",
		"Open Busy Modal",
		2,
		[](FEditorUIDrawContext& Ctx)
		{
			if (ImGui::MenuItem("Open Busy Modal") && Ctx.Registry)
			{
				Ctx.Registry->OpenModal("modal.busy");
			}
		} });

#if MAHO_EDITOR_DEMO_CONTENT
	UIRegistry.RegisterMenuItem({
		CatHelp,
		"menu.help.cvar",
		ICON_FA_CIRCLE_INFO "  CVar help",
		0,
		[this](FEditorUIDrawContext&)
		{
			if (ImGui::MenuItem(ICON_FA_CIRCLE_INFO "  CVar help"))
			{
				AppendOutput("Commands: `Dump` | `Name` | `Name Value` | `help`");
			}
		} });
	UIRegistry.RegisterMenuItem({
		CatWindow,
		"menu.window.imgui_demo",
		ICON_FA_TABLE_CELLS "  ImGui Demo",
		30,
		[this](FEditorUIDrawContext&)
		{
			ImGui::MenuItem(ICON_FA_TABLE_CELLS "  ImGui Demo", nullptr, &bShowDemoWindow);
		} });
	UIRegistry.RegisterMenuItem({
		CatWindow,
		"menu.window.implot_demo",
		ICON_FA_CHART_AREA "  ImPlot Demo",
		31,
		[this](FEditorUIDrawContext&)
		{
			ImGui::MenuItem(ICON_FA_CHART_AREA "  ImPlot Demo", nullptr, &bShowImPlotDemo);
		} });
#else
	(void)CatHelp;
#endif
}

void FEditorLayer::RegisterDummyUIContributions()
{
	const FEditorUICatalog CatDummy{ "Dummy", 900 };

	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarPrimary,
		CatDummy,
		"dummy.tb1.a",
		0,
		[this](FEditorUIDrawContext&)
		{
			if (!bShowDummyUI)
			{
				return;
			}
			ImGui::Button("DumA##Tb1");
		} });
	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarPrimary,
		CatDummy,
		"dummy.tb1.b",
		1,
		[this](FEditorUIDrawContext&)
		{
			if (!bShowDummyUI)
			{
				return;
			}
			ImGui::Button("DumB##Tb1");
		} });

	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarSecondary,
		CatDummy,
		"dummy.tb2.a",
		0,
		[this](FEditorUIDrawContext& Ctx)
		{
			if (!bShowDummyUI)
			{
				return;
			}
			const ImVec2 BtnSize(Ctx.ToolbarButtonSize, Ctx.ToolbarButtonSize);
			ImGui::Button("A##Tb2Dum", BtnSize);
		} });
	UIRegistry.RegisterToolbarItem({
		EEditorUIRegion::ToolbarSecondary,
		CatDummy,
		"dummy.tb2.b",
		1,
		[this](FEditorUIDrawContext& Ctx)
		{
			if (!bShowDummyUI)
			{
				return;
			}
			const ImVec2 BtnSize(Ctx.ToolbarButtonSize, Ctx.ToolbarButtonSize);
			ImGui::Button("B##Tb2Dum", BtnSize);
		} });

	UIRegistry.RegisterMenuItem({
		CatDummy,
		"dummy.menu.a",
		"Dummy Menu A",
		0,
		[this](FEditorUIDrawContext&)
		{
			if (!bShowDummyUI)
			{
				return;
			}
			ImGui::MenuItem("Dummy Menu A");
		} });
	UIRegistry.RegisterMenuItem({
		CatDummy,
		"dummy.menu.b",
		"Dummy Menu B",
		1,
		[this](FEditorUIDrawContext&)
		{
			if (!bShowDummyUI)
			{
				return;
			}
			ImGui::MenuItem("Dummy Menu B");
		} });

	UIRegistry.RegisterDockPanel({
		CatDummy,
		"dummy.dock.a",
		ICON_FA_CUBE "  Dummy Dock A",
		&bShowDummyDockA,
		false,
		true,
		0,
		[this](FEditorUIDrawContext&)
		{
			if (!bShowDummyUI)
			{
				return;
			}
			if (!BeginEditorDockPanel(ICON_FA_CUBE "  Dummy Dock A", &bShowDummyDockA))
			{
				ImGui::End();
				return;
			}
			ImGui::TextUnformatted("Dummy dock panel A (Catalog Dummy).");
			ImGui::End();
		} });
	UIRegistry.RegisterDockPanel({
		CatDummy,
		"dummy.dock.b",
		ICON_FA_CUBES "  Dummy Dock B",
		&bShowDummyDockB,
		false,
		true,
		1,
		[this](FEditorUIDrawContext&)
		{
			if (!bShowDummyUI)
			{
				return;
			}
			if (!BeginEditorDockPanel(ICON_FA_CUBES "  Dummy Dock B", &bShowDummyDockB))
			{
				ImGui::End();
				return;
			}
			ImGui::TextUnformatted("Dummy dock panel B (Catalog Dummy).");
			ImGui::End();
		} });

	UIRegistry.RegisterViewportOverlay({
		CatDummy,
		"dummy.viewport.a",
		0,
		[this](FEditorUIDrawContext&)
		{
			if (!bShowDummyUI)
			{
				return;
			}
			ImGui::SetCursorPos(ImVec2(12.0f, 12.0f));
			ImGui::TextDisabled("Dummy Overlay A");
		} });
	UIRegistry.RegisterViewportOverlay({
		CatDummy,
		"dummy.viewport.b",
		1,
		[this](FEditorUIDrawContext&)
		{
			if (!bShowDummyUI)
			{
				return;
			}
			ImGui::SetCursorPos(ImVec2(12.0f, 28.0f));
			ImGui::TextDisabled("Dummy Overlay B");
		} });

	UIRegistry.RegisterMenuItem({
		FEditorUICatalog{ "Debug", 100 },
		"menu.debug.open_dummy_docks",
		"Open Dummy Docks",
		3,
		[this](FEditorUIDrawContext& Ctx)
		{
			if (ImGui::MenuItem("Open Dummy Docks") && Ctx.Registry)
			{
				Ctx.Registry->OpenDockPanel("dummy.dock.a");
				Ctx.Registry->OpenDockPanel("dummy.dock.b");
			}
		} });
}

bool FEditorLayer::ExecuteStage(EEngineStage Stage)
{
	if (Stage == EEngineStage::Attach)
	{
#if !MAHO_EDITOR_DEMO_ONLY
		MountEditor();
#endif
		return true;
	}
	if (Stage == EEngineStage::Detach)
	{
#if !MAHO_EDITOR_DEMO_ONLY
		UnmountEditor();
#endif
		return true;
	}
	if (Stage != EEngineStage::Update)
	{
		return true;
	}
	if (ImGui::GetCurrentContext() == nullptr)
	{
		return true;
	}

#if MAHO_EDITOR_DEMO_ONLY
	static bool bShowDemoWindowDiag = true;
	ImGui::ShowDemoWindow(&bShowDemoWindowDiag);
	return true;
#else
	if (!GApp)
	{
		return true;
	}
	FApp& App = *GApp;

	DrainEngineLogs(App);

	if (AgentChat)
	{
		AgentChat->Tick();
		std::vector<FAgentChatBubble> Remote;
		AgentChat->DrainRemoteBubbles(Remote);
		for (FAgentChatBubble& Bubble : Remote)
		{
			AppendAgentBubble(Bubble.Role, std::move(Bubble.Text));
		}
	}

	DrawDockSpace(App);
	DrawMainViewportPanel();
	{
		FEditorUIDrawContext Ctx = MakeUIDrawContext(App);
		UIRegistry.DrawDockPanels(Ctx);
		UIRegistry.DrawModals(Ctx);
	}
#if MAHO_EDITOR_EXTRA_PANELS
	DrawFileDialogs();
#endif

#if MAHO_EDITOR_DEMO_CONTENT
	if (bShowDemoWindow)
	{
		ImGui::ShowDemoWindow(&bShowDemoWindow);
	}
	if (bShowImPlotDemo)
	{
		ImPlot::ShowDemoWindow(&bShowImPlotDemo);
	}
#endif
#endif
	return true;
}

void FEditorLayer::DrawMenuItems(FApp& App, float RowH)
{
	// Full-row-height menu buttons (BeginMenu in a MenuBar only hits on text height).
	auto DrawTopLevelMenu = [&](const char* Id, const char* Label, auto&& FillMenu)
	{
		const ImVec2 LabelSize = ImGui::CalcTextSize(Label);
		const float PadX = 10.0f;
		const ImVec2 BtnSize(LabelSize.x + PadX * 2.0f, RowH);

		ImGui::PushID(Id);
		const ImVec2 P0 = ImGui::GetCursorScreenPos();
		const bool bPopupOpen = ImGui::IsPopupOpen(Id);
		if (ImGui::InvisibleButton("##Hit", BtnSize))
		{
			ImGui::OpenPopup(Id);
		}
		const bool bHovered = ImGui::IsItemHovered();

		if (bHovered || bPopupOpen)
		{
			const ImU32 Col = ImGui::GetColorU32(bPopupOpen ? ImGuiCol_HeaderActive : ImGuiCol_HeaderHovered);
			ImGui::GetWindowDrawList()->AddRectFilled(P0, ImVec2(P0.x + BtnSize.x, P0.y + BtnSize.y), Col);
		}

		ImGui::GetWindowDrawList()->AddText(
			ImVec2(P0.x + PadX, P0.y + (RowH - LabelSize.y) * 0.5f),
			ImGui::GetColorU32(ImGuiCol_Text),
			Label);

		// Align popup to the menu button's left edge (not mouse cursor).
		ImGui::SetNextWindowPos(ImVec2(P0.x, P0.y + BtnSize.y));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 10.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));
		// Theme sets Separator alpha=0 for invisible dock gutters; menus need a visible line.
		ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1.0f, 1.0f, 1.0f, 0.22f));
		if (ImGui::BeginPopup(Id))
		{
			FillMenu();
			ImGui::EndPopup();
		}
		ImGui::PopStyleColor();
		ImGui::PopStyleVar(3);
		ImGui::PopID();
		ImGui::SameLine(0.0f, 0.0f);
	};

	DrawTopLevelMenu("MenuFile", "File", [&]()
	{
#if MAHO_EDITOR_DEMO_CONTENT
		if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Open..."))
		{
			IGFD::FileDialogConfig Config;
			Config.path = "Content";
			ImGuiFileDialog::Instance()->OpenDialog("EditorOpenDlg", "Open File", ".*", Config);
		}
		if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK "  Save As..."))
		{
			IGFD::FileDialogConfig Config;
			Config.path = "Content";
			ImGuiFileDialog::Instance()->OpenDialog("EditorSaveDlg", "Save File", ".*", Config);
		}
		ImGui::Separator();
		if (ImGui::MenuItem(ICON_FA_ARROWS_ROTATE "  Refresh Content Browser", "F5"))
		{
			RefreshContentListing();
			AppendOutput("Content browser refreshed.");
		}
		ImGui::Separator();
#endif
		if (ImGui::MenuItem(ICON_FA_RIGHT_FROM_BRACKET "  Exit"))
		{
			if (GApp)
			{
				GApp->OnRequestExit();
			}
		}
		FEditorUIDrawContext Ctx = MakeUIDrawContext(App);
		UIRegistry.DrawMenuPopup("File", Ctx);
	});

	DrawTopLevelMenu("MenuWindow", "Window", [&]()
	{
		FEditorUIDrawContext Ctx = MakeUIDrawContext(App);
		UIRegistry.DrawDockPanelMenuToggles(Ctx);
		ImGui::Separator();
		UIRegistry.DrawMenuPopup("Window", Ctx);
	});

	for (const FEditorUICatalog& Catalog : UIRegistry.GetMenuCatalogs())
	{
		if (Catalog.Name == "File" || Catalog.Name == "Window")
		{
			continue;
		}
		const std::string MenuName = Catalog.Name;
		const std::string MenuId = std::string("MenuDyn_") + MenuName;
		DrawTopLevelMenu(MenuId.c_str(), MenuName.c_str(), [&, MenuName]()
		{
			FEditorUIDrawContext Ctx = MakeUIDrawContext(App);
			UIRegistry.DrawMenuPopup(MenuName, Ctx);
		});
	}
}

void FEditorLayer::DrawBrandBlock(float Size)
{
	const ImVec4 BrandBg = ImVec4(14.0f / 255.0f, 14.0f / 255.0f, 16.0f / 255.0f, 1.0f);
	ImGui::PushStyleColor(ImGuiCol_ChildBg, BrandBg);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::BeginChild(
		"##EditorBrand",
		ImVec2(Size, Size),
		ImGuiChildFlags_None,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);

	const char* Glyph = ICON_FA_CAT;
	ImGui::SetWindowFontScale(1.75f);
	const ImVec2 Scaled = ImGui::CalcTextSize(Glyph);
	const ImVec2 Region = ImGui::GetContentRegionAvail();
	ImGui::SetCursorPos(ImVec2(
		(Region.x - Scaled.x) * 0.5f,
		(Region.y - Scaled.y) * 0.5f));
	ImGui::TextUnformatted(Glyph);
	ImGui::SetWindowFontScale(1.0f);

	ImGui::EndChild();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();
}

void FEditorLayer::DrawToolbarPrimary()
{
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 0.0f));
	if (GApp)
	{
		FEditorUIDrawContext Ctx = MakeUIDrawContext(*GApp);
		UIRegistry.DrawToolbar(EEditorUIRegion::ToolbarPrimary, Ctx);
	}
	ImGui::PopStyleVar();
}

void FEditorLayer::DrawToolbarSecondary()
{
	const float BtnH = ImGui::GetContentRegionAvail().y;
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 0.0f));
	if (GApp)
	{
		FEditorUIDrawContext Ctx = MakeUIDrawContext(*GApp);
		Ctx.ToolbarButtonSize = BtnH;
		UIRegistry.DrawToolbar(EEditorUIRegion::ToolbarSecondary, Ctx);
	}
	ImGui::PopStyleVar(2);
}

void FEditorLayer::EnsureDefaultDockLayout(std::uint32_t DockspaceId)
{
	ImGui::DockBuilderRemoveNode(DockspaceId);
	ImGui::DockBuilderAddNode(DockspaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(DockspaceId, ImGui::GetContentRegionAvail());

	// Central: locked MyGame viewport. Bottom: Content Browser + Output Log tabs.
	ImGuiID DockMain = DockspaceId;
	ImGuiID DockBottom = ImGui::DockBuilderSplitNode(DockMain, ImGuiDir_Down, 0.30f, nullptr, &DockMain);

	ImGui::DockBuilderDockWindow(kWinMainViewport, DockMain);
	ImGui::DockBuilderDockWindow(kWinContent, DockBottom);
	ImGui::DockBuilderDockWindow(kWinOutput, DockBottom);
	ImGui::DockBuilderDockWindow(kWinAgent, DockBottom);
	ImGui::DockBuilderDockWindow(kWinSequenceGraph, DockBottom);
	if (ImGuiDockNode* Central = ImGui::DockBuilderGetNode(DockMain))
	{
		Central->SetLocalFlags(static_cast<ImGuiDockNodeFlags>(
			static_cast<int>(Central->LocalFlags)
			| static_cast<int>(ImGuiDockNodeFlags_NoTabBar)
			| static_cast<int>(ImGuiDockNodeFlags_NoUndocking)));
	}
	if (ImGuiDockNode* Bottom = ImGui::DockBuilderGetNode(DockBottom))
	{
		Bottom->SetLocalFlags(static_cast<ImGuiDockNodeFlags>(
			static_cast<int>(Bottom->LocalFlags)
			| static_cast<int>(ImGuiDockNodeFlags_NoWindowMenuButton)
			| static_cast<int>(ImGuiDockNodeFlags_NoCloseButton)));
	}
#if MAHO_EDITOR_EXTRA_PANELS
	ImGuiID DockRight = ImGui::DockBuilderSplitNode(DockMain, ImGuiDir_Right, 0.28f, nullptr, &DockMain);
	ImGuiID DockBottomRight = ImGui::DockBuilderSplitNode(DockBottom, ImGuiDir_Right, 0.45f, nullptr, &DockBottom);
	ImGui::DockBuilderDockWindow(kWinBlueprint, DockRight);
	ImGui::DockBuilderDockWindow(kWinPlot, DockBottomRight);
	ImGui::DockBuilderDockWindow(kWinContent, DockBottom);
	ImGui::DockBuilderDockWindow(kWinOutput, DockBottom);
	ImGui::DockBuilderDockWindow(kWinAgent, DockBottom);
	ImGui::DockBuilderDockWindow(kWinSequenceGraph, DockBottom);
	if (ImGuiDockNode* Central = ImGui::DockBuilderGetNode(DockMain))
	{
		Central->SetLocalFlags(static_cast<ImGuiDockNodeFlags>(
			static_cast<int>(Central->LocalFlags)
			| static_cast<int>(ImGuiDockNodeFlags_NoTabBar)
			| static_cast<int>(ImGuiDockNodeFlags_NoUndocking)));
	}
#endif
	ImGui::DockBuilderFinish(DockspaceId);
}

void FEditorLayer::DrawDockSpace(FApp& App)
{
	ImGuiViewport* Viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(Viewport->WorkPos);
	ImGui::SetNextWindowSize(Viewport->WorkSize);
	ImGui::SetNextWindowViewport(Viewport->ID);

	const float OuterPad = 8.0f;
	const ImVec4 DockChassis = ImVec4(14.0f / 255.0f, 14.0f / 255.0f, 16.0f / 255.0f, 1.0f);
	const ImVec4 ChromeBg = DockChassis;
	const ImVec4 PlaceholderBg = ImVec4(38.0f / 255.0f, 39.0f / 255.0f, 43.0f / 255.0f, 1.0f);
	const ImVec4 MenuHover = ImVec4(52.0f / 255.0f, 54.0f / 255.0f, 60.0f / 255.0f, 1.0f);
	const ImVec4 MenuActive = ImVec4(66.0f / 255.0f, 70.0f / 255.0f, 78.0f / 255.0f, 1.0f);
	ImGuiWindowFlags RootFlags =
		ImGuiWindowFlags_NoDocking
		| ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoResize
		| ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoBringToFrontOnFocus
		| ImGuiWindowFlags_NoNavFocus
		| ImGuiWindowFlags_NoScrollbar
		| ImGuiWindowFlags_NoScrollWithMouse;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, DockChassis);
	ImGui::Begin("##EditorRoot", nullptr, RootFlags);
	ImGui::PopStyleVar(4);

	ImGuiStyle& Style = ImGui::GetStyle();
	const ImVec2 ThemeFramePadding = Style.FramePadding;
	// Brand is the vertical reference; menu and toolbar are each exactly half its height.
	const float BrandSize = ImMax(56.0f, ImGui::GetFrameHeight() * 2.0f + ThemeFramePadding.y * 2.0f);
	const float MenuRowH = BrandSize * 0.5f;
	const float ToolbarHeight = BrandSize * 0.5f;
	// Light-gray reserved strip under brand+toolbar.
	const float PlaceholderH = MenuRowH;

	ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.0f, 1.0f));

	// Brand spans menu + toolbar rows; menu and toolbar sit to its right.
	DrawBrandBlock(BrandSize);
	ImGui::SameLine(0.0f, 0.0f);

	// Kill MenuBar bottom hairline for the whole header (drawn with Border * FrameBorderSize).
	const float BackupFrameBorderSize = Style.FrameBorderSize;
	const ImVec4 BackupHeaderBorder = Style.Colors[ImGuiCol_Border];
	Style.FrameBorderSize = 0.0f;
	Style.Colors[ImGuiCol_Border] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

	ImGui::PushStyleColor(ImGuiCol_ChildBg, ChromeBg);
	ImGui::BeginChild(
		"##EditorHeaderRight",
		ImVec2(0.0f, BrandSize),
		ImGuiChildFlags_None,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);

	// Row 1: main menu bar — full-height buttons (half brand height).
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ChromeBg);
	ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
	ImGui::PushStyleColor(ImGuiCol_HeaderHovered, MenuHover);
	ImGui::PushStyleColor(ImGuiCol_HeaderActive, MenuActive);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::BeginChild(
		"##EditorMenuRow",
		ImVec2(0.0f, MenuRowH),
		ImGuiChildFlags_AlwaysUseWindowPadding,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
	DrawMenuItems(App, MenuRowH);
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor(4);

	// Row 2: pinned toolbar — exact remaining half of the brand height.
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ChromeBg);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 0.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
	ImGui::BeginChild(
		"##EditorToolbar",
		ImVec2(0.0f, ToolbarHeight),
		ImGuiChildFlags_AlwaysUseWindowPadding,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
	{
		const float Y = (ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeight()) * 0.5f;
		if (Y > 0.0f)
		{
			ImGui::SetCursorPosY(ImGui::GetCursorPosY() + Y);
		}
		DrawToolbarPrimary();
	}
	ImGui::EndChild();
	ImGui::PopStyleVar(2);
	ImGui::PopStyleColor();

	ImGui::EndChild(); // ##EditorHeaderRight
	ImGui::PopStyleColor();

	Style.FrameBorderSize = BackupFrameBorderSize;
	Style.Colors[ImGuiCol_Border] = BackupHeaderBorder;

	ImGui::PopStyleVar(7);

	// Toolbar 2: light-gray strip inset like the main dock (same OuterPad seam).
	ImGui::SetCursorPos(ImVec2(OuterPad, ImGui::GetCursorPosY() + OuterPad));
	{
		const float Toolbar2W = ImGui::GetContentRegionAvail().x - OuterPad;
		ImGui::PushStyleColor(ImGuiCol_ChildBg, PlaceholderBg);
		ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 2.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
		ImGui::BeginChild(
			"##EditorToolbar2",
			ImVec2(Toolbar2W, PlaceholderH),
			ImGuiChildFlags_AlwaysUseWindowPadding,
			ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
		DrawToolbarSecondary();
		ImGui::EndChild();
		ImGui::PopStyleVar(4);
		ImGui::PopStyleColor();
	}

	// Fixed main docking space for all editor windows
	ImGui::SetCursorPos(ImVec2(OuterPad, ImGui::GetCursorPosY() + OuterPad));
	const ImVec2 DockAvail = ImGui::GetContentRegionAvail();
	ImGui::PushStyleColor(ImGuiCol_ChildBg, DockChassis);
	ImGui::BeginChild(
		"##EditorDockHost",
		ImVec2(DockAvail.x - OuterPad, DockAvail.y - OuterPad),
		ImGuiChildFlags_None,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);

	const ImVec4 BackupBorder = Style.Colors[ImGuiCol_Border];
	Style.Colors[ImGuiCol_Border] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

	const ImGuiID DockspaceId = ImGui::GetID("MahoEditorMainDock_v5");
	if (bBuildDefaultLayout)
	{
		EnsureDefaultDockLayout(DockspaceId);
		bBuildDefaultLayout = false;
	}
	ImGui::DockSpace(DockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_NoDockingOverCentralNode);

	Style.Colors[ImGuiCol_Border] = BackupBorder;
	ImGui::EndChild();
	ImGui::PopStyleColor();

	ImGui::End();
	ImGui::PopStyleColor();
}

void FEditorLayer::DrawMainViewportPanel()
{
	// Locked into the central dock: no tab bar, cannot undock into a floating window.
	ImGuiWindowClass ViewportClass;
	ViewportClass.DockNodeFlagsOverrideSet = static_cast<ImGuiDockNodeFlags>(
		static_cast<int>(ImGuiDockNodeFlags_NoTabBar) | static_cast<int>(ImGuiDockNodeFlags_NoUndocking));
	ImGui::SetNextWindowClass(&ViewportClass);
	ImGui::Begin(kWinMainViewport, nullptr, ImGuiWindowFlags_NoCollapse);

	const ImVec2 Canvas = ImGui::GetContentRegionAvail();
	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	DrawList->AddRectFilled(Origin, ImVec2(Origin.x + Canvas.x, Origin.y + Canvas.y), IM_COL32(8, 9, 11, 255));

	ImGuizmo::SetOrthographic(false);
	ImGuizmo::SetDrawlist();
	ImGuizmo::SetRect(Origin.x, Origin.y, Canvas.x, Canvas.y);
	if (Canvas.x > 1.0f && Canvas.y > 1.0f)
	{
		PerspectiveRH(ProjectionMatrix, 45.0f * 3.14159265f / 180.0f, Canvas.x / Canvas.y, 0.1f, 100.0f);
		ImGuizmo::DrawGrid(ViewMatrix, ProjectionMatrix, ObjectMatrix, 10.0f);
		if (ViewportTool != EViewportTool::Select)
		{
			ImGuizmo::Manipulate(
				ViewMatrix,
				ProjectionMatrix,
				static_cast<ImGuizmo::OPERATION>(GizmoOperation),
				ImGuizmo::LOCAL,
				ObjectMatrix);
		}
	}
	ImGui::Dummy(Canvas);
	if (GApp)
	{
		FEditorUIDrawContext Ctx = MakeUIDrawContext(*GApp);
		UIRegistry.DrawViewportOverlays(Ctx);
	}
	ImGui::End();
}

void FEditorLayer::DrawTransientDetailsPanel()
{
	if (!BeginEditorDockPanel(kWinTransientDetails, &bShowTransientDetails))
	{
		ImGui::End();
		return;
	}
	ImGui::TextUnformatted("Temporary Details (DockPanel, not Modal).");
	ImGui::TextDisabled("Opened via Debug → Open Temporary Details / OpenDockPanel.");
	ImGui::End();
}

void FEditorLayer::DrawContentBrowser()
{
	// No trailing dock-bar close (X); show/hide via Window menu.
	ImGuiWindowClass ContentClass;
	ContentClass.DockNodeFlagsOverrideSet = static_cast<ImGuiDockNodeFlags>(
		static_cast<int>(ImGuiDockNodeFlags_NoWindowMenuButton)
		| static_cast<int>(ImGuiDockNodeFlags_NoCloseButton));
	ImGui::SetNextWindowClass(&ContentClass);
	if (!ImGui::Begin(kWinContent, nullptr, ImGuiWindowFlags_NoCollapse))
	{
		ImGui::End();
		return;
	}

	const bool bAtMountRoot =
		CurrentVirtualPath == "/Game" || CurrentVirtualPath == "/Engine";
	ImGui::BeginDisabled(bAtMountRoot);
	if (ImGui::SmallButton(ICON_FA_ARROW_UP "##ContentUp"))
	{
		const std::size_t Slash = CurrentVirtualPath.find_last_of('/');
		if (Slash != std::string::npos && Slash > 0)
		{
			SelectContentFolder(CurrentVirtualPath.substr(0, Slash));
		}
	}
	ImGui::EndDisabled();
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
	{
		ImGui::SetTooltip("Up to parent folder");
	}
	ImGui::SameLine(0.0f, 6.0f);

	// Clickable breadcrumbs: /Game / Maps / ...
	{
		std::string Accumulated;
		bool bFirst = true;
		std::size_t Index = 0;
		while (Index < CurrentVirtualPath.size())
		{
			if (CurrentVirtualPath[Index] == '/')
			{
				++Index;
				continue;
			}
			const std::size_t Next = CurrentVirtualPath.find('/', Index);
			const std::string Segment = CurrentVirtualPath.substr(
				Index,
				Next == std::string::npos ? std::string::npos : Next - Index);
			Accumulated += "/";
			Accumulated += Segment;

			if (!bFirst)
			{
				ImGui::SameLine(0.0f, 2.0f);
				ImGui::TextDisabled("/");
				ImGui::SameLine(0.0f, 2.0f);
			}
			bFirst = false;

			ImGui::PushID(static_cast<int>(Accumulated.size()));
			const bool bIsCurrent = (Accumulated == CurrentVirtualPath);
			if (bIsCurrent)
			{
				ImGui::TextUnformatted(Segment.c_str());
			}
			else if (ImGui::SmallButton(Segment.c_str()))
			{
				SelectContentFolder(Accumulated);
			}
			ImGui::PopID();

			if (Next == std::string::npos)
			{
				break;
			}
			Index = Next + 1;
		}
	}

	ImGui::SameLine();
	if (ImGui::SmallButton(ICON_FA_ARROWS_ROTATE "##ContentRefresh"))
	{
		RefreshContentListing();
	}

	const ImVec4 DeepBg = ImVec4(14.0f / 255.0f, 14.0f / 255.0f, 16.0f / 255.0f, 1.0f);
	const float TreeWidth = ImMax(180.0f, ImGui::GetContentRegionAvail().x * 0.22f);

	ImGui::PushStyleColor(ImGuiCol_ChildBg, DeepBg);
	ImGui::BeginChild(
		"##ContentTree",
		ImVec2(TreeWidth, 0.0f),
		ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
		ImGuiWindowFlags_HorizontalScrollbar);
	DrawContentBrowserTree();
	ImGui::EndChild();
	ImGui::PopStyleColor();

	ImGui::SameLine(0.0f, 0.0f);

	ImGui::PushStyleColor(ImGuiCol_ChildBg, DeepBg);
	ImGui::BeginChild(
		"##ContentTiles",
		ImVec2(0.0f, 0.0f),
		ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
	DrawContentBrowserTiles();
	ImGui::EndChild();
	ImGui::PopStyleColor();

	ImGui::End();
}

void FEditorLayer::DrawContentBrowserTree()
{
	std::error_code ErrorCode;
	for (const FPathMount& Mount : FPaths::GetMountPoints())
	{
		const std::filesystem::path DiskRoot = Mount.DiskRoot;
		ImGuiTreeNodeFlags RootFlags =
			ImGuiTreeNodeFlags_OpenOnArrow
			| ImGuiTreeNodeFlags_OpenOnDoubleClick
			| ImGuiTreeNodeFlags_SpanAvailWidth
			| ImGuiTreeNodeFlags_DefaultOpen;
		if (CurrentVirtualPath == Mount.VirtualRoot)
		{
			RootFlags |= ImGuiTreeNodeFlags_Selected;
		}

		ImGui::PushID(Mount.VirtualRoot.c_str());
		const bool bOpen = ImGui::TreeNodeEx(
			"##Root",
			RootFlags,
			ICON_FA_FOLDER_TREE "  %s",
			Mount.VirtualRoot.c_str());
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
		{
			SelectContentFolder(Mount.VirtualRoot);
		}
		if (bOpen)
		{
			if (std::filesystem::is_directory(DiskRoot, ErrorCode) && !ErrorCode)
			{
				DrawVirtualFolderTree(Mount.VirtualRoot, DiskRoot, 0);
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
}

void FEditorLayer::DrawVirtualFolderTree(
	const std::string& VirtualPath,
	const std::filesystem::path& DiskPath,
	int Depth)
{
	(void)Depth;
	std::error_code ErrorCode;
	std::vector<std::filesystem::path> Subdirs;
	for (const std::filesystem::directory_entry& Entry :
		std::filesystem::directory_iterator(DiskPath, ErrorCode))
	{
		if (ErrorCode)
		{
			break;
		}
		const std::string EntryName = Entry.path().filename().string();
		if (EntryName.empty() || EntryName[0] == '.')
		{
			continue;
		}
		if (Entry.is_directory(ErrorCode) && !ErrorCode)
		{
			Subdirs.push_back(Entry.path());
		}
	}
	std::sort(Subdirs.begin(), Subdirs.end());

	for (const std::filesystem::path& Sub : Subdirs)
	{
		const std::string ChildName = Sub.filename().string();
		const std::string ChildVirtual = VirtualPath + "/" + ChildName;
		ImGuiTreeNodeFlags Flags =
			ImGuiTreeNodeFlags_OpenOnArrow
			| ImGuiTreeNodeFlags_OpenOnDoubleClick
			| ImGuiTreeNodeFlags_SpanAvailWidth;
		if (CurrentVirtualPath == ChildVirtual)
		{
			Flags |= ImGuiTreeNodeFlags_Selected;
		}

		ImGui::PushID(ChildVirtual.c_str());
		const bool bOpen = ImGui::TreeNodeEx(
			"##Dir",
			Flags,
			ICON_FA_FOLDER "  %s",
			ChildName.c_str());
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
		{
			SelectContentFolder(ChildVirtual);
		}
		if (bOpen)
		{
			DrawVirtualFolderTree(ChildVirtual, Sub, Depth + 1);
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
}

void FEditorLayer::DrawContentBrowserTiles()
{
	const float Tile = 88.0f;
	const float Pad = 8.0f;
	const float LabelGap = 4.0f;
	const float Cell = Tile + Pad;
	const float AvailX = ImGui::GetContentRegionAvail().x;
	int Columns = static_cast<int>(AvailX / Cell);
	if (Columns < 1)
	{
		Columns = 1;
	}

	auto DrawTile = [&](const std::string& VirtualPath, bool bIsFolder)
	{
		const std::size_t Slash = VirtualPath.find_last_of('/');
		const std::string EntryName =
			(Slash == std::string::npos) ? VirtualPath : VirtualPath.substr(Slash + 1);
		const bool bSelected = SelectedVirtualEntry == VirtualPath;
		const char* Glyph = bIsFolder ? ICON_FA_FOLDER : ICON_FA_FILE;
		const ImU32 GlyphColor = bIsFolder
			? IM_COL32(242, 199, 89, 255)
			: IM_COL32(199, 209, 224, 255);
		const ImU32 FaceColor = bSelected
			? IM_COL32(51, 71, 102, 255)
			: IM_COL32(31, 33, 38, 255);
		const ImU32 FaceHover = IM_COL32(56, 61, 71, 255);

		ImGui::PushID(VirtualPath.c_str());
		ImGui::BeginGroup();

		const ImVec2 StartPos = ImGui::GetCursorPos();
		const ImVec2 Screen0 = ImGui::GetCursorScreenPos();
		ImGui::InvisibleButton("##Tile", ImVec2(Tile, Tile));
		const bool bHovered = ImGui::IsItemHovered();
		if (ImGui::IsItemClicked())
		{
			SelectedVirtualEntry = VirtualPath;
		}
		if (bIsFolder && bHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			SelectContentFolder(VirtualPath);
		}

		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		DrawList->AddRectFilled(
			Screen0,
			ImVec2(Screen0.x + Tile, Screen0.y + Tile),
			bHovered ? FaceHover : FaceColor,
			4.0f);

		ImFont* Font = ImGui::GetFont();
		const float IconPx = Tile * 0.72f;
		const ImVec2 GlyphSize = Font->CalcTextSizeA(IconPx, FLT_MAX, 0.0f, Glyph);
		DrawList->AddText(
			Font,
			IconPx,
			ImVec2(
				Screen0.x + (Tile - GlyphSize.x) * 0.5f,
				Screen0.y + (Tile - GlyphSize.y) * 0.5f),
			GlyphColor,
			Glyph);

		const ImVec2 LabelSize = ImGui::CalcTextSize(EntryName.c_str());
		ImGui::SetCursorPos(ImVec2(
			StartPos.x + (Tile - LabelSize.x) * 0.5f,
			StartPos.y + Tile + LabelGap));
		ImGui::TextUnformatted(EntryName.c_str());

		// Keep group cell width for SameLine column layout.
		ImGui::SetCursorPos(ImVec2(StartPos.x, StartPos.y + Tile + LabelGap + LabelSize.y));
		ImGui::Dummy(ImVec2(Tile, 0.0f));

		ImGui::EndGroup();
		ImGui::PopID();
	};

	int Index = 0;
	for (const std::string& Folder : FolderVirtualEntries)
	{
		if (Index > 0 && (Index % Columns) != 0)
		{
			ImGui::SameLine(0.0f, Pad);
		}
		DrawTile(Folder, true);
		++Index;
	}
	for (const std::string& File : FileVirtualEntries)
	{
		if (Index > 0 && (Index % Columns) != 0)
		{
			ImGui::SameLine(0.0f, Pad);
		}
		DrawTile(File, false);
		++Index;
	}

	if (FolderVirtualEntries.empty() && FileVirtualEntries.empty())
	{
		ImGui::TextDisabled("Empty  %s", CurrentVirtualPath.c_str());
	}
}

void FEditorLayer::EnsureContentMounts()
{
	FPaths::EnsureMountDirectories();
}

void FEditorLayer::SelectContentFolder(const std::string& VirtualPath)
{
	CurrentVirtualPath = VirtualPath.empty() ? "/Game" : FPaths::NormalizePackagePath(VirtualPath);
	SelectedVirtualEntry.clear();
	RefreshContentListing();
}

std::filesystem::path FEditorLayer::VirtualPathToDisk(const std::string& VirtualPath) const
{
	const std::string Disk = FPaths::ConvertVirtualPathToFilename(VirtualPath);
	if (!Disk.empty())
	{
		return std::filesystem::path(Disk);
	}
	return std::filesystem::path(FPaths::GetProjectContentDir());
}

void FEditorLayer::RefreshContentListing()
{
	FolderVirtualEntries.clear();
	FileVirtualEntries.clear();

	const std::filesystem::path DiskFolder = VirtualPathToDisk(CurrentVirtualPath);
	std::error_code ErrorCode;
	if (!std::filesystem::is_directory(DiskFolder, ErrorCode) || ErrorCode)
	{
		return;
	}

	for (const std::filesystem::directory_entry& Entry :
		std::filesystem::directory_iterator(DiskFolder, ErrorCode))
	{
		if (ErrorCode)
		{
			break;
		}
		const std::string EntryName = Entry.path().filename().string();
		if (EntryName.empty() || EntryName[0] == '.')
		{
			continue;
		}
		const std::string ChildVirtual = CurrentVirtualPath + "/" + EntryName;
		if (Entry.is_directory(ErrorCode) && !ErrorCode)
		{
			FolderVirtualEntries.push_back(ChildVirtual);
		}
		else if (Entry.is_regular_file(ErrorCode) && !ErrorCode)
		{
			FileVirtualEntries.push_back(ChildVirtual);
		}
	}

	std::sort(FolderVirtualEntries.begin(), FolderVirtualEntries.end());
	std::sort(FileVirtualEntries.begin(), FileVirtualEntries.end());
}

void FEditorLayer::DrawOutputPanel(FApp& App)
{
	ImGuiWindowClass OutputClass;
	OutputClass.DockNodeFlagsOverrideSet = static_cast<ImGuiDockNodeFlags>(
		static_cast<int>(ImGuiDockNodeFlags_NoWindowMenuButton)
		| static_cast<int>(ImGuiDockNodeFlags_NoCloseButton));
	ImGui::SetNextWindowClass(&OutputClass);
	if (!ImGui::Begin(kWinOutput, nullptr, ImGuiWindowFlags_NoCollapse))
	{
		ImGui::End();
		return;
	}

	const float Footer = ImGui::GetFrameHeightWithSpacing() + 4.0f;
	ImGui::BeginChild("##OutputScroll", ImVec2(0.0f, -Footer), ImGuiChildFlags_AlwaysUseWindowPadding);
	ImGuiListClipper Clipper;
	Clipper.Begin(static_cast<int>(OutputLines.size()));
	while (Clipper.Step())
	{
		for (int Index = Clipper.DisplayStart; Index < Clipper.DisplayEnd; ++Index)
		{
			const FOutputLine& Line = OutputLines[static_cast<std::size_t>(Index)];
			ImGui::PushStyleColor(ImGuiCol_Text, OutputColorForLevel(Line.Level));
			ImGui::TextUnformatted(Line.Text.c_str());
			ImGui::PopStyleColor();
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

void FEditorLayer::DrawAgentPanel()
{
	ImGuiWindowClass AgentClass;
	AgentClass.DockNodeFlagsOverrideSet = static_cast<ImGuiDockNodeFlags>(
		static_cast<int>(ImGuiDockNodeFlags_NoWindowMenuButton)
		| static_cast<int>(ImGuiDockNodeFlags_NoCloseButton));
	ImGui::SetNextWindowClass(&AgentClass);
	if (!ImGui::Begin(kWinAgent, nullptr, ImGuiWindowFlags_NoCollapse))
	{
		ImGui::End();
		return;
	}

	const std::string Status = AgentChat ? AgentChat->GetStatusText() : "offline";
	const bool bConnected = AgentChat && AgentChat->IsConnected();
	const bool bBusy = AgentChat && AgentChat->IsBusy();
	const bool bMock = AgentChat && AgentChat->IsMockMode();

	ImGui::TextDisabled("Status: %s%s%s",
		Status.c_str(),
		bConnected ? " | connected" : " | disconnected",
		bMock ? " | mock" : "");
	ImGui::Separator();

	const float Footer = ImGui::GetFrameHeightWithSpacing() + 4.0f;
	ImGui::BeginChild("##AgentScroll", ImVec2(0.0f, -Footer), ImGuiChildFlags_AlwaysUseWindowPadding);
	for (const FAgentBubble& Bubble : AgentBubbles)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, AgentColorForRole(Bubble.Role));
		ImGui::TextUnformatted(AgentRoleLabel(Bubble.Role));
		ImGui::PopStyleColor();
		ImGui::PushTextWrapPos(0.0f);
		ImGui::TextUnformatted(Bubble.Text.c_str());
		ImGui::PopTextWrapPos();
		ImGui::Spacing();
	}
	if (bBusy)
	{
		ImGui::TextDisabled("Agent is thinking...");
	}
	if (bAutoScrollAgent && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
	{
		ImGui::SetScrollHereY(1.0f);
	}
	ImGui::EndChild();

	ImGui::BeginDisabled(bBusy || !AgentChat);
	ImGui::SetNextItemWidth(-1.0f);
	if (ImGui::InputText(
			"##AgentInput",
			AgentInput,
			IM_ARRAYSIZE(AgentInput),
			ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_EscapeClearsAll))
	{
		const std::string Line = AgentInput;
		AgentInput[0] = '\0';
		SendAgentMessage(Line);
		ImGui::SetKeyboardFocusHere(-1);
	}
	ImGui::EndDisabled();
	ImGui::End();
}

void FEditorLayer::EnsureSequenceGraphNodeLayout()
{
	EnsureSequenceGraphNodeLayout({});
}

void FEditorLayer::EnsureSequenceGraphNodeLayout(const std::vector<IEngineExtension*>& Extensions)
{
	if (bSequenceGraphLayoutApplied || !SequenceGraphEditorContext)
	{
		return;
	}

	if (SequenceGraphViewMode == 0)
	{
		const float OriginX = 40.0f;
		const float OriginY = 40.0f;
		const float ColW = 260.0f;
		const float RowH = 110.0f;
		int BandCounts[3] = {};

		for (std::size_t Index = 0; Index < Extensions.size(); ++Index)
		{
			IEngineExtension* Extension = Extensions[Index];
			if (!Extension)
			{
				continue;
			}

			int Band = static_cast<int>(Extension->GetPriority());
			if (Band < 0 || Band > 2)
			{
				Band = 0;
			}

			const float X = OriginX + static_cast<float>(Band) * ColW;
			const float Y = OriginY + static_cast<float>(BandCounts[Band]) * RowH;
			++BandCounts[Band];

			ed::SetNodePosition(
				ed::NodeId(SeqGraphIds::ExtNodeBase + static_cast<int>(Index)),
				ImVec2(X, Y));
		}
	}
	else
	{
		const float X = 0.0f;
		const float RowH = 80.0f;
		for (int i = 0; i < 8; ++i)
		{
			ed::SetNodePosition(
				ed::NodeId(SeqGraphIds::LifeNodeBase + i),
				ImVec2(X, static_cast<float>(i) * RowH));
		}
	}

	ed::NavigateToContent(0.15f);
	bSequenceGraphLayoutApplied = true;
}

void FEditorLayer::DrawSequenceGraphPanel(FApp& App)
{
	if (!BeginEditorDockPanel(kWinSequenceGraph, &bShowSequenceGraphPanel))
	{
		ImGui::End();
		return;
	}

	if (!bSequenceGraphEditorInited || !SequenceGraphEditorContext)
	{
		ImGui::TextDisabled("Sequence Graph node editor context unavailable.");
		ImGui::End();
		return;
	}

	ImGui::TextDisabled(
		"State %s | Frame %llu | dt %.3f | fixedDt %.3f",
		AppStateLabel(App.GetState()),
		static_cast<unsigned long long>(App.GetFrameIndex()),
		App.GetDeltaSeconds(),
		App.GetFixedDeltaSeconds());
	ImGui::SameLine();
	ImGui::Dummy(ImVec2(12.0f, 0.0f));
	ImGui::SameLine();
	if (ImGui::RadioButton("Extension depends", SequenceGraphViewMode == 0))
	{
		SequenceGraphViewMode = 0;
		bSequenceGraphLayoutApplied = false;
	}
	ImGui::SameLine();
	if (ImGui::RadioButton("FApp::Run lifecycle", SequenceGraphViewMode == 1))
	{
		SequenceGraphViewMode = 1;
		bSequenceGraphLayoutApplied = false;
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset Layout"))
	{
		bSequenceGraphLayoutApplied = false;
	}

	std::vector<IEngineExtension*> GraphExtensions;
	std::vector<FExtensionDepEdgeView> GraphEdges;
	if (SequenceGraphViewMode == 0)
	{
		ImGui::SameLine(0.0f, 16.0f);
		ImGui::SetNextItemWidth(160.0f);
		if (ImGui::BeginCombo("##SeqGraphStage", EngineStageLabel(static_cast<EEngineStage>(SequenceGraphStage))))
		{
			const int StageCount = static_cast<int>(EEngineStage::COUNT);
			for (int Stage = 0; Stage < StageCount; ++Stage)
			{
				const bool bSelected = (SequenceGraphStage == Stage);
				if (ImGui::Selectable(EngineStageLabel(static_cast<EEngineStage>(Stage)), bSelected))
				{
					SequenceGraphStage = Stage;
					bSequenceGraphLayoutApplied = false;
				}
				if (bSelected)
				{
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		GraphExtensions = App.SnapshotExtensions();
		GraphEdges = App.SnapshotExtensionDeps(static_cast<EEngineStage>(SequenceGraphStage));

		if (GraphExtensions.size() != SequenceGraphLayoutExtCount
			|| SequenceGraphStage != SequenceGraphLayoutStage)
		{
			SequenceGraphLayoutExtCount = GraphExtensions.size();
			SequenceGraphLayoutStage = SequenceGraphStage;
			bSequenceGraphLayoutApplied = false;
		}
	}

	ImGui::Separator();
	if (SequenceGraphViewMode == 0)
	{
		ImGui::TextColored(ImVec4(0.55f, 0.78f, 1.0f, 1.0f), "System");
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.70f, 0.95f, 0.55f, 1.0f), "Layer");
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.45f, 1.0f), "Overlay");
		ImGui::SameLine(0.0f, 16.0f);
		ImGui::TextColored(ImVec4(0.35f, 0.75f, 1.0f, 1.0f), "Strong");
		ImGui::SameLine();
		ImGui::TextDisabled("Dep → Self");
		ImGui::SameLine(0.0f, 16.0f);
		ImGui::TextColored(ImVec4(0.85f, 0.45f, 0.95f, 1.0f), "Weak");
		ImGui::SameLine();
		ImGui::TextDisabled("optional (exit stages)");
		ImGui::SameLine(0.0f, 16.0f);
		ImGui::TextDisabled(
			"%zu extension(s), %zu edge(s) @ %s",
			GraphExtensions.size(),
			GraphEdges.size(),
			EngineStageLabel(static_cast<EEngineStage>(SequenceGraphStage)));
	}
	else
	{
		ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.40f, 1.0f), "Lifecycle");
		ImGui::SameLine();
		ImGui::TextDisabled("boot → tick → shutdown");
	}

	ed::SetCurrentEditor(static_cast<ed::EditorContext*>(SequenceGraphEditorContext));
	{
		ed::Style& NodeStyle = ed::GetStyle();
		const ImVec4 PanelBg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
		NodeStyle.Colors[ed::StyleColor_Bg] = PanelBg;
		NodeStyle.Colors[ed::StyleColor_Grid] = ImVec4(1.0f, 1.0f, 1.0f, 0.04f);
	}

	ed::Begin("SequenceGraphCanvas");

	int LinkSerial = 30000;

	if (SequenceGraphViewMode == 0)
	{
		for (std::size_t Index = 0; Index < GraphExtensions.size(); ++Index)
		{
			IEngineExtension* Extension = GraphExtensions[Index];
			if (!Extension)
			{
				continue;
			}

			const int NodeIndex = static_cast<int>(Index);
			const EExtensionPriority Band = Extension->GetPriority();
			const char* ExtName = Extension->GetName();

			ed::BeginNode(ed::NodeId(SeqGraphIds::ExtNodeBase + NodeIndex));
			ImGui::TextColored(ExtensionPriorityColor(Band), "%s", ExtensionPriorityLabel(Band));
			ImGui::TextUnformatted(ExtName ? ExtName : "?");
			ed::BeginPin(ed::PinId(SeqGraphIds::ExtInPinBase + NodeIndex), ed::PinKind::Input);
			ImGui::Text(ICON_FA_ARROW_LEFT " in");
			ed::EndPin();
			ImGui::SameLine();
			ed::BeginPin(ed::PinId(SeqGraphIds::ExtOutPinBase + NodeIndex), ed::PinKind::Output);
			ImGui::Text("out " ICON_FA_ARROW_RIGHT);
			ed::EndPin();
			ed::EndNode();
		}

		const ImVec4 StrongColor(0.35f, 0.75f, 1.0f, 0.95f);
		const ImVec4 WeakColor(0.85f, 0.45f, 0.95f, 0.90f);
		for (const FExtensionDepEdgeView& Edge : GraphEdges)
		{
			const int From = FindExtensionIndex(GraphExtensions, Edge.Predecessor);
			const int To = FindExtensionIndex(GraphExtensions, Edge.Successor);
			if (From < 0 || To < 0 || From == To)
			{
				continue;
			}

			const bool bWeak = (Edge.Strength == EExtensionDepStrength::Weak);
			ed::Link(
				ed::LinkId(LinkSerial++),
				ed::PinId(SeqGraphIds::ExtOutPinBase + From),
				ed::PinId(SeqGraphIds::ExtInPinBase + To),
				bWeak ? WeakColor : StrongColor,
				bWeak ? 1.5f : 2.5f);
		}
	}
	else
	{
		struct FLifeStep
		{
			const char* Title;
			const char* Detail;
		};
		const FLifeStep Steps[] = {
			{"Configure", "Game fills FConfig"},
			{"FPaths + Ini", "Roots / DefaultEngine.ini"},
			{"Generate App", "codegen RegisterExtension (+ plugins)"},
			{"InitExtensions", "PreInit/Init/PostInit by Priority+depends"},
			{"Attach", "All extensions Attach"},
			{"PostInitialize", "optional game hook after Attach"},
			{"TickGroups", "BeginFrame…PostRender topo per stage"},
			{"Shutdown", "PrepareExit/Detach/Shutdown topo"},
		};
		const int StepCount = static_cast<int>(sizeof(Steps) / sizeof(Steps[0]));

		for (int i = 0; i < StepCount; ++i)
		{
			ed::BeginNode(ed::NodeId(SeqGraphIds::LifeNodeBase + i));
			ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.40f, 1.0f), "%s", Steps[i].Title);
			ImGui::TextDisabled("%s", Steps[i].Detail);
			if (i > 0)
			{
				ed::BeginPin(ed::PinId(SeqGraphIds::LifeInPinBase + i), ed::PinKind::Input);
				ImGui::Text(ICON_FA_ARROW_LEFT " in");
				ed::EndPin();
			}
			if (i + 1 < StepCount)
			{
				ed::BeginPin(ed::PinId(SeqGraphIds::LifeOutPinBase + i), ed::PinKind::Output);
				ImGui::Text("out " ICON_FA_ARROW_RIGHT);
				ed::EndPin();
			}
			ed::EndNode();
		}

		for (int i = 0; i + 1 < StepCount; ++i)
		{
			ed::Link(
				ed::LinkId(LinkSerial++),
				ed::PinId(SeqGraphIds::LifeOutPinBase + i),
				ed::PinId(SeqGraphIds::LifeInPinBase + i + 1),
				ImVec4(1.0f, 0.82f, 0.35f, 0.95f),
				2.0f);
		}
	}

	if (!bSequenceGraphLayoutApplied)
	{
		if (SequenceGraphViewMode == 0)
		{
			EnsureSequenceGraphNodeLayout(GraphExtensions);
		}
		else
		{
			EnsureSequenceGraphNodeLayout();
		}
	}

	ed::End();
	ed::SetCurrentEditor(nullptr);

	ImGui::End();
}

void FEditorLayer::DrawBlueprintPanel()
{
	if (!BeginEditorDockPanel(kWinBlueprint, &bShowBlueprintPanel))
	{
		ImGui::End();
		return;
	}
#if MAHO_EDITOR_DEMO_CONTENT
	if (!bBlueprintInited || !NodeEditorContext)
	{
		ImGui::TextDisabled("Node editor context unavailable.");
		ImGui::End();
		return;
	}

	ed::SetCurrentEditor(static_cast<ed::EditorContext*>(NodeEditorContext));
	// Match Maho panel WindowBg so the canvas does not read as a lighter child sheet.
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
#endif
	ImGui::End();
}

void FEditorLayer::DrawPlotPanel()
{
	if (!BeginEditorDockPanel(kWinPlot, &bShowPlotPanel))
	{
		ImGui::End();
		return;
	}
#if MAHO_EDITOR_DEMO_CONTENT
	static float Values[90] = {};
	static int Offset = 0;
	Values[Offset] = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 3.0f);
	Offset = (Offset + 1) % IM_ARRAYSIZE(Values);

	if (ImPlot::BeginPlot("Signal", ImVec2(-1, -1)))
	{
		ImPlot::PlotLine("sin", Values, IM_ARRAYSIZE(Values), 1.0, 0, ImPlotLineFlags_None, Offset);
		ImPlot::EndPlot();
	}
#endif
	ImGui::End();
}

void FEditorLayer::DrawFileDialogs()
{
#if MAHO_EDITOR_DEMO_CONTENT
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
#endif
}

void FEditorLayer::AppendOutput(std::string Line, spdlog::level::level_enum Level)
{
	FOutputLine Entry;
	Entry.Text = std::move(Line);
	Entry.Level = Level;
	OutputLines.push_back(std::move(Entry));
	while (OutputLines.size() > MaxOutputLines)
	{
		OutputLines.pop_front();
	}
}

void FEditorLayer::StartAgentChat()
{
	namespace fs = std::filesystem;

	FAgentChatStartOptions Options;
	Options.ProjectCwd = fs::current_path().string();
	Options.Port = 8765;
	Options.bSpawnBridge = true;

	bool bAutoConnect = true;
	FConfigFile EditorIni;
	std::vector<std::filesystem::path> IniCandidates;
	IniCandidates.push_back(fs::current_path() / "Config" / "DefaultEditor.ini");
#if defined(_WIN32)
	{
		wchar_t ModulePathW[MAX_PATH] = {};
		if (GetModuleFileNameW(nullptr, ModulePathW, MAX_PATH) > 0)
		{
			IniCandidates.push_back(fs::path(ModulePathW).parent_path() / "Config" / "DefaultEditor.ini");
		}
	}
#endif
	bool bIniLoaded = false;
	for (const fs::path& Candidate : IniCandidates)
	{
		if (EditorIni.Load(Candidate.string()))
		{
			bIniLoaded = true;
			AppendAgentBubble(
				EAgentChatRole::System,
				std::string("Reading editor config: ") + Candidate.string());
			break;
		}
	}
	if (bIniLoaded)
	{
		(void)EditorIni.TryGetString("Agent", "ApiKey", Options.ApiKey);
		(void)EditorIni.TryGetInt("Agent", "BridgePort", Options.Port);
		(void)EditorIni.TryGetBool("Agent", "bAutoConnect", bAutoConnect);
		// Trim whitespace / quotes around the key.
		while (!Options.ApiKey.empty()
			&& (Options.ApiKey.front() == ' ' || Options.ApiKey.front() == '"' || Options.ApiKey.front() == '\''))
		{
			Options.ApiKey.erase(Options.ApiKey.begin());
		}
		while (!Options.ApiKey.empty()
			&& (Options.ApiKey.back() == ' ' || Options.ApiKey.back() == '"' || Options.ApiKey.back() == '\''))
		{
			Options.ApiKey.pop_back();
		}
	}

	if (!bAutoConnect)
	{
		AppendAgentBubble(
			EAgentChatRole::System,
			"Agent auto-connect disabled ([Agent] bAutoConnect=False in DefaultEditor.ini).");
		return;
	}

	const fs::path EngineRoot(MAHO_ENGINE_ROOT);
	const fs::path BridgeA = EngineRoot / "Tools" / "AgentBridge";
	const fs::path BridgeB = fs::current_path() / "Tools" / "AgentBridge";
	const fs::path BridgeC = fs::current_path().parent_path() / "Maho" / "Tools" / "AgentBridge";
	if (fs::exists(BridgeA / "server.mjs"))
	{
		Options.BridgeDirectory = BridgeA.string();
	}
	else if (fs::exists(BridgeB / "server.mjs"))
	{
		Options.BridgeDirectory = BridgeB.string();
	}
	else if (fs::exists(BridgeC / "server.mjs"))
	{
		Options.BridgeDirectory = BridgeC.string();
	}
	else
	{
		Options.BridgeDirectory = BridgeA.string();
	}

	AgentChat = std::make_unique<FAgentChatClient>();
	AgentChat->Start(Options);

	std::string Hello = "Connecting to Agent bridge...";
	if (!Options.ApiKey.empty())
	{
		Hello += "\nApiKey loaded from Config/DefaultEditor.ini.";
	}
	else
	{
		Hello += "\nNo ApiKey in Config/DefaultEditor.ini — bridge will use mock mode "
			"(or CURSOR_API_KEY from the environment).";
	}
	AppendAgentBubble(EAgentChatRole::System, std::move(Hello));
}

void FEditorLayer::AppendAgentBubble(EAgentChatRole Role, std::string Text)
{
	FAgentBubble Bubble;
	Bubble.Role = Role;
	Bubble.Text = std::move(Text);
	AgentBubbles.push_back(std::move(Bubble));
	while (AgentBubbles.size() > MaxAgentBubbles)
	{
		AgentBubbles.pop_front();
	}
}

void FEditorLayer::SendAgentMessage(std::string Text)
{
	while (!Text.empty() && (Text.back() == '\n' || Text.back() == '\r' || Text.back() == ' '))
	{
		Text.pop_back();
	}
	if (Text.empty() || !AgentChat)
	{
		return;
	}
	AppendAgentBubble(EAgentChatRole::User, Text);
	AgentChat->SendUserMessage(std::move(Text));
}

void FEditorLayer::DrainEngineLogs(FApp& App)
{
	std::vector<FCapturedLogLine> Captured;
	App.GetLog().DrainCapturedLines(Captured);
	for (FCapturedLogLine& Line : Captured)
	{
		AppendOutput(std::move(Line.Text), Line.Level);
	}
}

void FEditorLayer::ExecuteConsoleLine(FApp& App, const std::string& RawLine)
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
		FConsole& Console = App.GetConsole();
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

	FConsole& Console = App.GetConsole();
	IConsoleVariable* Variable = Console.Find(CVarName.c_str());
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
	if (!Console.SetFromString(CVarName.c_str(), Value.c_str(), Maho::EConsoleVariableSetBy::Console))
	{
		AppendOutput("Failed to set " + CVarName);
		return;
	}
	AppendOutput(CVarName + " = " + Variable->GetString());
}

} // namespace Maho
