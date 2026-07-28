#pragma once

#include <Render/RHI/RHI.h>

#include <vector>

#include <vulkan/vulkan.h>

namespace Catty
{

/** Minimal Vulkan implementation of IRHI. Not part of the public Catty API surface. */
class FVulkanRHI final : public IRHI
{
public:
	FVulkanRHI() = default;
	~FVulkanRHI() override;

	virtual bool Initialize(const FRHIInitDesc& Desc) override;
	virtual void Shutdown() override;

	virtual void BeginFrame() override;
	virtual void Clear(float R, float G, float B, float A) override;
	virtual void EndFrame() override;

	virtual void Resize(int Width, int Height) override;

	[[nodiscard]] virtual bool IsInitialized() const override;

	/** Begin command buffer + main swapchain render pass (clear). Leaves the pass open. */
	void BeginMainPass(float R, float G, float B, float A);

	/** End main render pass + command buffer. */
	void EndMainPass();

	[[nodiscard]] VkInstance GetVkInstance() const { return Instance; }
	[[nodiscard]] VkPhysicalDevice GetVkPhysicalDevice() const { return PhysicalDevice; }
	[[nodiscard]] VkDevice GetVkDevice() const { return Device; }
	[[nodiscard]] VkQueue GetGraphicsQueue() const { return GraphicsQueue; }
	[[nodiscard]] std::uint32_t GetGraphicsQueueFamilyIndex() const { return GraphicsQueueFamilyIndex; }
	[[nodiscard]] VkRenderPass GetVkRenderPass() const { return RenderPass; }
	[[nodiscard]] VkCommandBuffer GetVkCommandBuffer() const { return CommandBuffer; }
	[[nodiscard]] std::uint32_t GetSwapchainImageCount() const { return static_cast<std::uint32_t>(SwapchainImages.size()); }
	[[nodiscard]] std::uint32_t GetMinImageCount() const;

private:
	bool CreateInstance();
	bool CreateSurface();
	bool PickPhysicalDevice();
	bool CreateLogicalDevice();
	bool CreateSwapchain();
	void DestroySwapchainResources();
	bool CreateImageViews();
	bool CreateRenderPass();
	bool CreateFramebuffers();
	bool CreateCommandPoolAndBuffer();
	bool CreateSyncObjects();
	bool RecreateSwapchain();

	[[nodiscard]] bool IsDeviceSuitable(VkPhysicalDevice InPhysicalDevice) const;
	[[nodiscard]] bool FindQueueFamilies(VkPhysicalDevice InPhysicalDevice, std::uint32_t& OutGraphicsFamily, std::uint32_t& OutPresentFamily) const;
	[[nodiscard]] bool CheckDeviceExtensionSupport(VkPhysicalDevice InPhysicalDevice) const;

	void* NativeWindowHandle = nullptr;
	int FramebufferWidth = 0;
	int FramebufferHeight = 0;

	bool bInitialized = false;

	VkInstance Instance = VK_NULL_HANDLE;
	VkSurfaceKHR Surface = VK_NULL_HANDLE;
	VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
	VkDevice Device = VK_NULL_HANDLE;
	VkQueue GraphicsQueue = VK_NULL_HANDLE;
	VkQueue PresentQueue = VK_NULL_HANDLE;

	VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
	VkFormat SwapchainImageFormat = VK_FORMAT_UNDEFINED;
	VkExtent2D SwapchainExtent{};
	std::vector<VkImage> SwapchainImages;
	std::vector<VkImageView> SwapchainImageViews;
	std::vector<VkFramebuffer> SwapchainFramebuffers;

	VkRenderPass RenderPass = VK_NULL_HANDLE;

	VkCommandPool CommandPool = VK_NULL_HANDLE;
	VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;

	VkSemaphore ImageAvailableSemaphore = VK_NULL_HANDLE;
	VkSemaphore RenderFinishedSemaphore = VK_NULL_HANDLE;
	VkFence InFlightFence = VK_NULL_HANDLE;

	std::uint32_t GraphicsQueueFamilyIndex = 0;
	std::uint32_t PresentQueueFamilyIndex = 0;
	std::uint32_t CurrentImageIndex = 0;

	float ClearColorR = 0.0f;
	float ClearColorG = 0.0f;
	float ClearColorB = 0.0f;
	float ClearColorA = 1.0f;

	bool bFramebufferResized = false;
};

} // namespace Catty
