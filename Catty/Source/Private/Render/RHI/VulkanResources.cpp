#include "Render/RHI/VulkanResources.h"

namespace Catty
{

FVulkanBuffer::~FVulkanBuffer()
{
	if (Allocator != nullptr && Buffer != VK_NULL_HANDLE)
	{
		Allocator->DestroyBuffer(Buffer, Allocation);
		Buffer = VK_NULL_HANDLE;
		Allocation = nullptr;
	}
}

FVulkanTexture::~FVulkanTexture()
{
	if (Allocator != nullptr && Image != VK_NULL_HANDLE)
	{
		Allocator->DestroyImage(Image, Allocation);
		Image = VK_NULL_HANDLE;
		Allocation = nullptr;
	}
}

FVulkanSampler::~FVulkanSampler()
{
	if (Device != VK_NULL_HANDLE && Sampler != VK_NULL_HANDLE)
	{
		vkDestroySampler(Device, Sampler, nullptr);
		Sampler = VK_NULL_HANDLE;
	}
}

FVulkanShaderModule::~FVulkanShaderModule()
{
	if (Device != VK_NULL_HANDLE && Module != VK_NULL_HANDLE)
	{
		vkDestroyShaderModule(Device, Module, nullptr);
		Module = VK_NULL_HANDLE;
	}
}

FVulkanGraphicsPipeline::~FVulkanGraphicsPipeline()
{
	if (Device != VK_NULL_HANDLE && Pipeline != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(Device, Pipeline, nullptr);
		Pipeline = VK_NULL_HANDLE;
	}
}

FVulkanComputePipeline::~FVulkanComputePipeline()
{
	if (Device != VK_NULL_HANDLE && Pipeline != VK_NULL_HANDLE)
	{
		vkDestroyPipeline(Device, Pipeline, nullptr);
		Pipeline = VK_NULL_HANDLE;
	}
}

FVulkanFence::~FVulkanFence()
{
	if (Device != VK_NULL_HANDLE && Fence != VK_NULL_HANDLE)
	{
		vkDestroyFence(Device, Fence, nullptr);
		Fence = VK_NULL_HANDLE;
	}
}

FVulkanSemaphore::~FVulkanSemaphore()
{
	if (Device != VK_NULL_HANDLE && Semaphore != VK_NULL_HANDLE)
	{
		vkDestroySemaphore(Device, Semaphore, nullptr);
		Semaphore = VK_NULL_HANDLE;
	}
}

} // namespace Catty
