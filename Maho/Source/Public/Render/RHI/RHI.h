#pragma once

#include <Core/Export.h>
#include <Render/RHI/RHICommandList.h>
#include <Render/RHI/RHIEnums.h>
#include <Render/RHI/RHIResourceManager.h>
#include <Render/RHI/RHIResources.h>

#include <cstdint>
#include <limits>
#include <memory>

namespace Maho
{

struct FRHIInitDesc
{
	ERHIBackend Backend = ERHIBackend::Vulkan;

	/** Native window handle for WSI (HWND on Win32). */
	void* NativeWindowHandle = nullptr;

	int FramebufferWidth = 0;
	int FramebufferHeight = 0;
};

/**
 * Render-hardware interface. Public surface has no Vulkan / VMA types.
 *
 * Device creates resources; prefer FRHIResourceManager for Acquire/Release.
 * Always exposes Graphics / Compute / Transfer logical queues.
 */
class MAHO_API IRHI
{
public:
	virtual ~IRHI() = default;

	IRHI(const IRHI&) = delete;
	IRHI& operator=(const IRHI&) = delete;

	virtual bool Initialize(const FRHIInitDesc& Desc) = 0;
	virtual void Shutdown() = 0;

	virtual void BeginFrame() = 0;
	virtual void Clear(float R, float G, float B, float A) = 0;
	virtual void EndFrame() = 0;

	virtual void Resize(int Width, int Height) = 0;

	[[nodiscard]] virtual bool IsInitialized() const = 0;

	[[nodiscard]] virtual FRHIResourceManager& GetResourceManager() = 0;
	[[nodiscard]] virtual IRHIMemoryAllocator* GetMemoryAllocator() = 0;

	[[nodiscard]] virtual FRHIQueue& GetGraphicsQueue() = 0;
	[[nodiscard]] virtual FRHIQueue& GetComputeQueue() = 0;
	[[nodiscard]] virtual FRHIQueue& GetTransferQueue() = 0;

	[[nodiscard]] virtual FRHICommandList* CreateCommandList(ERHICommandListType Type) = 0;
	virtual void DestroyCommandList(FRHICommandList* CmdList) = 0;

	[[nodiscard]] virtual FRHIFence* CreateFence(bool bSignaled) = 0;
	virtual void DestroyFence(FRHIFence* Fence) = 0;
	virtual void WaitForFence(FRHIFence* Fence, std::uint64_t TimeoutNs = std::numeric_limits<std::uint64_t>::max()) = 0;

	[[nodiscard]] virtual FRHISemaphore* CreateGpuSemaphore() = 0;
	virtual void DestroyGpuSemaphore(FRHISemaphore* Semaphore) = 0;

	virtual void UpdateBuffer(FRHIBuffer* Buffer, std::uint64_t Offset, std::uint64_t Size, const void* Data) = 0;
	virtual void UpdateDescriptorSets(const FRHIDescriptorWrite* Writes, std::uint32_t Count) = 0;

	/** Internal factories used by FRHIResourceManager. Prefer Acquire* on the manager. */
	[[nodiscard]] virtual FRHIBuffer* CreateBuffer(const FRHIBufferDesc& Desc) = 0;
	virtual void DestroyBuffer(FRHIBuffer* Buffer) = 0;
	[[nodiscard]] virtual FRHITexture* CreateTexture(const FRHITextureDesc& Desc) = 0;
	virtual void DestroyTexture(FRHITexture* Texture) = 0;
	[[nodiscard]] virtual FRHISampler* CreateSampler(const FRHISamplerDesc& Desc) = 0;
	virtual void DestroySampler(FRHISampler* Sampler) = 0;
	[[nodiscard]] virtual FRHIShaderModule* CreateShaderModule(const FRHIShaderModuleDesc& Desc) = 0;
	virtual void DestroyShaderModule(FRHIShaderModule* Module) = 0;
	[[nodiscard]] virtual FRHIGraphicsPipeline* CreateGraphicsPipeline(const FRHIGraphicsPipelineDesc& Desc) = 0;
	virtual void DestroyGraphicsPipeline(FRHIGraphicsPipeline* Pipeline) = 0;
	[[nodiscard]] virtual FRHIComputePipeline* CreateComputePipeline(const FRHIComputePipelineDesc& Desc) = 0;
	virtual void DestroyComputePipeline(FRHIComputePipeline* Pipeline) = 0;

protected:
	IRHI() = default;
};

/** Deleter that frees the object inside Maho.dll (safe across EXE/DLL heaps). */
struct MAHO_API FRHIDeleter
{
	void operator()(IRHI* RHI) const;
};

using FRHIPtr = std::unique_ptr<IRHI, FRHIDeleter>;

/** Creates the RHI backend selected by FRHIInitDesc::Backend. */
class MAHO_API FRHIFactory
{
public:
	FRHIFactory() = delete;

	[[nodiscard]] static FRHIPtr Create(ERHIBackend Backend);
};

} // namespace Maho
