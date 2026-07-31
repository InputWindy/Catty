#include <Render/UI/ImGuiSystem.h>

#include <Core/System/Console.h>
#include <Core/System/Log.h>
#include <Core/System/PlatformWindow.h>
#include <Render/RHI/RHIServer.h>
#include <Render/UI/ImGuiTheme.h>
#include "Render/RHI/VulkanRHI.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <ImGuizmo.h>
#include <implot.h>

#include <GLFW/glfw3.h>

#include <atomic>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <vulkan/vulkan.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace Catty
{

namespace
{

static TAutoConsoleVariable GCVarImGuiDescriptorPoolSize(
	"r.ImGui.DescriptorPoolSize",
	16,
	"ImGui Vulkan descriptor pool size");

void CheckImGuiVkResult(VkResult Result)
{
	if (Result == 0)
	{
		return;
	}
	CATTY_CORE_ERROR("ImGui Vulkan error: VkResult = {}", static_cast<int>(Result));
}

/** Directory that contains Catty.dll / the game exe (fonts are copied next to binaries). */
[[nodiscard]] std::filesystem::path GetBinaryDirectory()
{
#if defined(_WIN32)
	HMODULE Module = nullptr;
	if (GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&GetBinaryDirectory),
			&Module) &&
		Module != nullptr)
	{
		wchar_t Buffer[MAX_PATH]{};
		const DWORD Length = GetModuleFileNameW(Module, Buffer, MAX_PATH);
		if (Length > 0 && Length < MAX_PATH)
		{
			return std::filesystem::path(Buffer).parent_path();
		}
	}
#endif
	return std::filesystem::current_path();
}

/** Prefer binary-adjacent Engine/Fonts, then cwd / Config fallbacks. */
[[nodiscard]] std::filesystem::path ResolveFontPath(
	const std::filesystem::path& FontFile,
	const std::filesystem::path& BinaryDir,
	const std::string& ConfigDirectory)
{
	namespace fs = std::filesystem;
	const fs::path Candidates[] = {
		BinaryDir / "Engine" / "Fonts" / FontFile,
		fs::current_path() / "Engine" / "Fonts" / FontFile,
		fs::path(ConfigDirectory) / ".." / "Engine" / "Fonts" / FontFile,
		fs::path(ConfigDirectory) / "Fonts" / FontFile,
	};
	for (const fs::path& Candidate : Candidates)
	{
		const fs::path Normalized = Candidate.lexically_normal();
		std::error_code ErrorCode;
		if (fs::exists(Normalized, ErrorCode) && !ErrorCode)
		{
			return Normalized;
		}
	}
	return {};
}

[[nodiscard]] ImFont* TryAddUiFont(ImGuiIO& IO, const std::filesystem::path& FontPath, float SizePixels)
{
	std::error_code ErrorCode;
	if (FontPath.empty() || !std::filesystem::exists(FontPath, ErrorCode) || ErrorCode)
	{
		return nullptr;
	}

	ImFontConfig Config;
	Config.OversampleH = 2;
	Config.OversampleV = 1;
	Config.PixelSnapH = true;
	return IO.Fonts->AddFontFromFileTTF(FontPath.string().c_str(), SizePixels, &Config);
}

[[nodiscard]] bool TryMergeFontAwesome(ImGuiIO& IO, const std::filesystem::path& FontPath, float SizePixels)
{
	std::error_code ErrorCode;
	if (FontPath.empty() || !std::filesystem::exists(FontPath, ErrorCode) || ErrorCode)
	{
		return false;
	}

	// Font Awesome glyphs need ~2/3 of the UI size to optically align with Latin text.
	ImFontConfig IconsConfig;
	IconsConfig.MergeMode = true;
	IconsConfig.PixelSnapH = true;
	IconsConfig.GlyphMinAdvanceX = SizePixels;
	static const ImWchar IconRanges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
	return IO.Fonts->AddFontFromFileTTF(
		FontPath.string().c_str(),
		SizePixels,
		&IconsConfig,
		IconRanges) != nullptr;
}

void LoadEditorFonts(ImGuiIO& IO, const std::string& ConfigDirectory)
{
	namespace fs = std::filesystem;
	constexpr float kUiFontSize = 16.0f;
	constexpr float kIconFontSize = kUiFontSize * 2.0f / 3.0f;

	const fs::path BinaryDir = GetBinaryDirectory();
	ImFont* UiFont = nullptr;

	// Primary: Inter (OFL). Fallback: Roboto Medium (Apache 2.0, shipped with imgui samples).
	const char* UiFontCandidates[] = {
		"Inter-Regular.ttf",
		"Roboto-Medium.ttf",
	};
	for (const char* FontFile : UiFontCandidates)
	{
		const fs::path Path = ResolveFontPath(FontFile, BinaryDir, ConfigDirectory);
		UiFont = TryAddUiFont(IO, Path, kUiFontSize);
		if (UiFont)
		{
			CATTY_CORE_INFO("FImGuiSystem: loaded UI font '{}' @ {:.0f}px", Path.string(), kUiFontSize);
			break;
		}
	}

#if defined(_WIN32)
	// Last resort on developer machines if Engine/Fonts was not copied.
	if (!UiFont)
	{
		const fs::path Segoe = fs::path(L"C:\\Windows\\Fonts\\segoeui.ttf");
		UiFont = TryAddUiFont(IO, Segoe, kUiFontSize);
		if (UiFont)
		{
			CATTY_CORE_INFO("FImGuiSystem: loaded UI font '{}'", Segoe.string());
		}
	}
#endif

	if (!UiFont)
	{
		UiFont = IO.Fonts->AddFontDefault();
		CATTY_CORE_WARN("FImGuiSystem: UI font missing; using Proggy default");
	}

	const fs::path IconPath = ResolveFontPath(FONT_ICON_FILE_NAME_FAS, BinaryDir, ConfigDirectory);
	if (TryMergeFontAwesome(IO, IconPath, kIconFontSize))
	{
		CATTY_CORE_INFO("FImGuiSystem: merged icon font '{}'", IconPath.string());
	}
	else
	{
		CATTY_CORE_WARN(
			"FImGuiSystem: Font Awesome not found (expected Engine/Fonts/{} next to binary or cwd)",
			FONT_ICON_FILE_NAME_FAS);
	}

	IO.FontDefault = UiFont;
}

} // namespace

FImGuiSystem::~FImGuiSystem()
{
	// Prefer Shutdown(RHIServer) from FRenderServer so Vulkan backends die before the device.
}

bool FImGuiSystem::Initialize(
	FPlatformWindow& Window,
	FRHIServer& RHIServer,
	const std::string& ConfigDirectory)
{
	if (bInitialized)
	{
		return true;
	}

	if (!Window.HasOsWindow())
	{
		CATTY_CORE_INFO("FImGuiSystem: skipped (no OS window)");
		return false;
	}

	void* ToolkitHandle = Window.GetToolkitWindowHandle();
	if (!ToolkitHandle)
	{
		CATTY_CORE_ERROR("FImGuiSystem::Initialize: toolkit window handle is null");
		return false;
	}

	FVulkanRHI* VulkanRHI = RHIServer.GetVulkanRHI();
	if (!VulkanRHI || !VulkanRHI->IsInitialized())
	{
		CATTY_CORE_ERROR("FImGuiSystem::Initialize: Vulkan RHI is not ready");
		return false;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImPlot::CreateContext();
	ImGuiIO& IO = ImGui::GetIO();
	IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	IO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	{
		namespace fs = std::filesystem;
		std::error_code ErrorCode;
		const fs::path ConfigDir = ConfigDirectory.empty() ? fs::path("Config") : fs::path(ConfigDirectory);
		fs::create_directories(ConfigDir, ErrorCode);
		IniFilePath = (ConfigDir / "imgui.ini").string();
		IO.IniFilename = IniFilePath.c_str();
		CATTY_CORE_INFO("FImGuiSystem: ini path '{}'", IniFilePath);
	}

	LoadEditorFonts(IO, ConfigDirectory);

	ImGui::StyleColorsDark();
	ApplyCattyNightTheme();

	GLFWwindow* GlfwWindow = static_cast<GLFWwindow*>(ToolkitHandle);
	if (!ImGui_ImplGlfw_InitForVulkan(GlfwWindow, true))
	{
		CATTY_CORE_ERROR("FImGuiSystem::Initialize: ImGui_ImplGlfw_InitForVulkan failed");
		ImPlot::DestroyContext();
		ImGui::DestroyContext();
		return false;
	}

	std::atomic<bool> bVulkanBackendOk{false};
	RHIServer.Enqueue([VulkanRHI, &bVulkanBackendOk](FThreadedServer& /*Server*/)
	{
		ImGui_ImplVulkan_InitInfo InitInfo{};
		InitInfo.ApiVersion = VK_API_VERSION_1_2;
		InitInfo.Instance = VulkanRHI->GetVkInstance();
		InitInfo.PhysicalDevice = VulkanRHI->GetVkPhysicalDevice();
		InitInfo.Device = VulkanRHI->GetVkDevice();
		InitInfo.QueueFamily = VulkanRHI->GetGraphicsQueueFamilyIndex();
		InitInfo.Queue = VulkanRHI->GetVkGraphicsQueue();
		InitInfo.DescriptorPool = VK_NULL_HANDLE;
		InitInfo.DescriptorPoolSize = static_cast<std::uint32_t>(
			(std::max)(1, GCVarImGuiDescriptorPoolSize.GetValue()));
		InitInfo.RenderPass = VulkanRHI->GetVkRenderPass();
		InitInfo.MinImageCount = VulkanRHI->GetMinImageCount();
		InitInfo.ImageCount = VulkanRHI->GetSwapchainImageCount();
		InitInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		InitInfo.Subpass = 0;
		InitInfo.CheckVkResultFn = CheckImGuiVkResult;

		bVulkanBackendOk.store(ImGui_ImplVulkan_Init(&InitInfo));
	});
	RHIServer.Flush();

	if (!bVulkanBackendOk.load())
	{
		CATTY_CORE_ERROR("FImGuiSystem::Initialize: ImGui_ImplVulkan_Init failed");
		ImGui_ImplGlfw_Shutdown();
		ImPlot::DestroyContext();
		ImGui::DestroyContext();
		return false;
	}

	bInitialized = true;
	CATTY_CORE_INFO("FImGuiSystem initialized (GLFW + Vulkan, docking)");
	return true;
}

void FImGuiSystem::Shutdown(FRHIServer& RHIServer)
{
	if (!bInitialized)
	{
		return;
	}

	RHIServer.Flush();

	RHIServer.Enqueue([](FThreadedServer& /*Server*/)
	{
		ImGui_ImplVulkan_Shutdown();
	});
	RHIServer.Flush();

	ImGui_ImplGlfw_Shutdown();
	ImPlot::DestroyContext();
	ImGui::DestroyContext();

	IniFilePath.clear();
	bInitialized = false;
	CATTY_CORE_INFO("FImGuiSystem shut down");
}

void FImGuiSystem::BeginFrame()
{
	if (!bInitialized)
	{
		return;
	}

	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
	ImGuizmo::BeginFrame();
}

void FImGuiSystem::EndFrame()
{
	if (!bInitialized)
	{
		return;
	}

	ImGui::Render();
}

bool FImGuiSystem::PollExitRequest() const
{
	if (!bInitialized)
	{
		return false;
	}

	const ImGuiIO& IO = ImGui::GetIO();
	return !IO.WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_Escape);
}

} // namespace Catty
