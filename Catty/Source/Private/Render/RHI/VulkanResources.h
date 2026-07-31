#pragma once

#include <Render/RHI/RHIResources.h>

#include "Render/RHI/VulkanMemory.h"

#include <vulkan/vulkan.h>

namespace Catty
{

class FVulkanBuffer final : public FRHIBuffer
{
public:
	FVulkanBuffer(FRHIBufferDesc InDesc, VkBuffer InBuffer, VmaAllocation InAllocation, FVulkanMemoryAllocator* InAllocator)
		: Desc(InDesc)
		, Buffer(InBuffer)
		, Allocation(InAllocation)
		, Allocator(InAllocator)
	{
	}

	~FVulkanBuffer() override;

	[[nodiscard]] const FRHIBufferDesc& GetDesc() const override { return Desc; }
	[[nodiscard]] VkBuffer GetVkBuffer() const { return Buffer; }
	[[nodiscard]] VmaAllocation GetAllocation() const { return Allocation; }

private:
	FRHIBufferDesc Desc{};
	VkBuffer Buffer = VK_NULL_HANDLE;
	VmaAllocation Allocation = nullptr;
	FVulkanMemoryAllocator* Allocator = nullptr;
};

class FVulkanTexture final : public FRHITexture
{
public:
	FVulkanTexture(FRHITextureDesc InDesc, VkImage InImage, VmaAllocation InAllocation, FVulkanMemoryAllocator* InAllocator)
		: Desc(InDesc)
		, Image(InImage)
		, Allocation(InAllocation)
		, Allocator(InAllocator)
	{
	}

	~FVulkanTexture() override;

	[[nodiscard]] const FRHITextureDesc& GetDesc() const override { return Desc; }
	[[nodiscard]] VkImage GetVkImage() const { return Image; }

private:
	FRHITextureDesc Desc{};
	VkImage Image = VK_NULL_HANDLE;
	VmaAllocation Allocation = nullptr;
	FVulkanMemoryAllocator* Allocator = nullptr;
};

class FVulkanSampler final : public FRHISampler
{
public:
	explicit FVulkanSampler(FRHISamplerDesc InDesc, VkDevice InDevice, VkSampler InSampler)
		: Desc(InDesc)
		, Device(InDevice)
		, Sampler(InSampler)
	{
	}

	~FVulkanSampler() override;

	[[nodiscard]] const FRHISamplerDesc& GetDesc() const override { return Desc; }
	[[nodiscard]] VkSampler GetVkSampler() const { return Sampler; }

private:
	FRHISamplerDesc Desc{};
	VkDevice Device = VK_NULL_HANDLE;
	VkSampler Sampler = VK_NULL_HANDLE;
};

class FVulkanShaderModule final : public FRHIShaderModule
{
public:
	FVulkanShaderModule(VkDevice InDevice, VkShaderModule InModule)
		: Device(InDevice)
		, Module(InModule)
	{
	}

	~FVulkanShaderModule() override;

	[[nodiscard]] VkShaderModule GetVkShaderModule() const { return Module; }

private:
	VkDevice Device = VK_NULL_HANDLE;
	VkShaderModule Module = VK_NULL_HANDLE;
};

class FVulkanGraphicsPipeline final : public FRHIGraphicsPipeline
{
public:
	FVulkanGraphicsPipeline(VkDevice InDevice, VkPipeline InPipeline)
		: Device(InDevice)
		, Pipeline(InPipeline)
	{
	}

	~FVulkanGraphicsPipeline() override;

	[[nodiscard]] VkPipeline GetVkPipeline() const { return Pipeline; }

private:
	VkDevice Device = VK_NULL_HANDLE;
	VkPipeline Pipeline = VK_NULL_HANDLE;
};

class FVulkanComputePipeline final : public FRHIComputePipeline
{
public:
	FVulkanComputePipeline(VkDevice InDevice, VkPipeline InPipeline)
		: Device(InDevice)
		, Pipeline(InPipeline)
	{
	}

	~FVulkanComputePipeline() override;

	[[nodiscard]] VkPipeline GetVkPipeline() const { return Pipeline; }

private:
	VkDevice Device = VK_NULL_HANDLE;
	VkPipeline Pipeline = VK_NULL_HANDLE;
};

class FVulkanFence final : public FRHIFence
{
public:
	FVulkanFence(VkDevice InDevice, VkFence InFence)
		: Device(InDevice)
		, Fence(InFence)
	{
	}

	~FVulkanFence() override;

	[[nodiscard]] VkFence GetVkFence() const { return Fence; }

private:
	VkDevice Device = VK_NULL_HANDLE;
	VkFence Fence = VK_NULL_HANDLE;
};

class FVulkanSemaphore final : public FRHISemaphore
{
public:
	FVulkanSemaphore(VkDevice InDevice, VkSemaphore InSemaphore)
		: Device(InDevice)
		, Semaphore(InSemaphore)
	{
	}

	~FVulkanSemaphore() override;

	[[nodiscard]] VkSemaphore GetVkSemaphore() const { return Semaphore; }

private:
	VkDevice Device = VK_NULL_HANDLE;
	VkSemaphore Semaphore = VK_NULL_HANDLE;
};

} // namespace Catty
