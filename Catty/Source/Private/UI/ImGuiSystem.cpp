#include "Catty/UI/ImGuiSystem.h"

#include "Catty/Core/ConsoleManager.h"
#include "Catty/Core/Log.h"
#include "Catty/Platform/PlatformWindow.h"
#include "Catty/Render/RenderServer.h"
#include "RHI/VulkanRHI.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <GLFW/glfw3.h>

#include <atomic>
#include <algorithm>
#include <cstdint>
#include <vulkan/vulkan.h>

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

} // namespace

FImGuiSystem::~FImGuiSystem()
{
	// Prefer Shutdown(RenderServer) from FApp so Vulkan backends die before the device.
}

bool FImGuiSystem::Initialize(FPlatformWindow& Window, FRenderServer& RenderServer)
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

	FVulkanRHI* VulkanRHI = RenderServer.GetVulkanRHI();
	if (!VulkanRHI || !VulkanRHI->IsInitialized())
	{
		CATTY_CORE_ERROR("FImGuiSystem::Initialize: Vulkan RHI is not ready");
		return false;
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& IO = ImGui::GetIO();
	IO.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	ImGui::StyleColorsDark();

	GLFWwindow* GlfwWindow = static_cast<GLFWwindow*>(ToolkitHandle);
	if (!ImGui_ImplGlfw_InitForVulkan(GlfwWindow, true))
	{
		CATTY_CORE_ERROR("FImGuiSystem::Initialize: ImGui_ImplGlfw_InitForVulkan failed");
		ImGui::DestroyContext();
		return false;
	}

	std::atomic<bool> bVulkanBackendOk{false};
	RenderServer.Enqueue([VulkanRHI, &bVulkanBackendOk](FThreadedServer& /*Server*/)
	{
		ImGui_ImplVulkan_InitInfo InitInfo{};
		InitInfo.ApiVersion = VK_API_VERSION_1_2;
		InitInfo.Instance = VulkanRHI->GetVkInstance();
		InitInfo.PhysicalDevice = VulkanRHI->GetVkPhysicalDevice();
		InitInfo.Device = VulkanRHI->GetVkDevice();
		InitInfo.QueueFamily = VulkanRHI->GetGraphicsQueueFamilyIndex();
		InitInfo.Queue = VulkanRHI->GetGraphicsQueue();
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
	RenderServer.Flush();

	if (!bVulkanBackendOk.load())
	{
		CATTY_CORE_ERROR("FImGuiSystem::Initialize: ImGui_ImplVulkan_Init failed");
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
		return false;
	}

	RenderServer.SetImGuiEnabled(true);
	bInitialized = true;
	CATTY_CORE_INFO("FImGuiSystem initialized (GLFW + Vulkan)");
	return true;
}

void FImGuiSystem::Shutdown(FRenderServer& RenderServer)
{
	if (!bInitialized)
	{
		return;
	}

	RenderServer.SetImGuiEnabled(false);
	RenderServer.Flush();

	RenderServer.Enqueue([](FThreadedServer& /*Server*/)
	{
		ImGui_ImplVulkan_Shutdown();
	});
	RenderServer.Flush();

	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

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
}

void FImGuiSystem::EndFrame()
{
	if (!bInitialized)
	{
		return;
	}

	ImGui::Render();
}

} // namespace Catty
