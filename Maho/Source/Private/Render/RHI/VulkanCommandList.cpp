#include "Render/RHI/VulkanCommandList.h"

#include "Render/RHI/VulkanResources.h"

#include <Core/System/Log.h>

#include <vector>

namespace Maho
{

namespace
{

[[nodiscard]] VkPipelineStageFlags ToVkPipelineStage(ERHIResourceState State)
{
	switch (State)
	{
	case ERHIResourceState::VertexBuffer:
		return VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
	case ERHIResourceState::IndexBuffer:
		return VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
	case ERHIResourceState::UniformBuffer:
		return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	case ERHIResourceState::ShaderResource:
		return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	case ERHIResourceState::UnorderedAccess:
		return VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
	case ERHIResourceState::RenderTarget:
		return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	case ERHIResourceState::DepthWrite:
		return VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
	case ERHIResourceState::CopySrc:
	case ERHIResourceState::CopyDst:
		return VK_PIPELINE_STAGE_TRANSFER_BIT;
	case ERHIResourceState::Present:
		return VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	default:
		return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	}
}

[[nodiscard]] VkAccessFlags ToVkAccess(ERHIResourceState State)
{
	switch (State)
	{
	case ERHIResourceState::VertexBuffer:
		return VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
	case ERHIResourceState::IndexBuffer:
		return VK_ACCESS_INDEX_READ_BIT;
	case ERHIResourceState::UniformBuffer:
		return VK_ACCESS_UNIFORM_READ_BIT;
	case ERHIResourceState::ShaderResource:
		return VK_ACCESS_SHADER_READ_BIT;
	case ERHIResourceState::UnorderedAccess:
		return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	case ERHIResourceState::RenderTarget:
		return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	case ERHIResourceState::DepthWrite:
		return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	case ERHIResourceState::CopySrc:
		return VK_ACCESS_TRANSFER_READ_BIT;
	case ERHIResourceState::CopyDst:
		return VK_ACCESS_TRANSFER_WRITE_BIT;
	default:
		return 0;
	}
}

} // namespace

void FVulkanQueue::Submit(
	FRHICommandList* const* CmdLists,
	std::uint32_t Count,
	FRHISemaphore* const* WaitSemaphores,
	std::uint32_t WaitCount,
	FRHISemaphore* const* SignalSemaphores,
	std::uint32_t SignalCount,
	FRHIFence* SignalFence)
{
	if (NativeQueue == VK_NULL_HANDLE)
	{
		MAHO_CORE_ERROR("FVulkanQueue::Submit: native queue is null");
		return;
	}

	std::vector<VkCommandBuffer> VkBuffers;
	VkBuffers.reserve(Count);
	for (std::uint32_t Index = 0; Index < Count; ++Index)
	{
		auto* VulkanCL = static_cast<FVulkanCommandList*>(CmdLists[Index]);
		if (VulkanCL == nullptr || VulkanCL->GetVkCommandBuffer() == VK_NULL_HANDLE)
		{
			MAHO_CORE_ERROR("FVulkanQueue::Submit: invalid command list");
			return;
		}
		VkBuffers.push_back(VulkanCL->GetVkCommandBuffer());
	}

	std::vector<VkSemaphore> WaitVk;
	std::vector<VkPipelineStageFlags> WaitStages;
	WaitVk.reserve(WaitCount);
	WaitStages.reserve(WaitCount);
	for (std::uint32_t Index = 0; Index < WaitCount; ++Index)
	{
		auto* Sem = static_cast<FVulkanSemaphore*>(WaitSemaphores[Index]);
		if (Sem == nullptr)
		{
			continue;
		}
		WaitVk.push_back(Sem->GetVkSemaphore());
		WaitStages.push_back(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
	}

	std::vector<VkSemaphore> SignalVk;
	SignalVk.reserve(SignalCount);
	for (std::uint32_t Index = 0; Index < SignalCount; ++Index)
	{
		auto* Sem = static_cast<FVulkanSemaphore*>(SignalSemaphores[Index]);
		if (Sem == nullptr)
		{
			continue;
		}
		SignalVk.push_back(Sem->GetVkSemaphore());
	}

	VkSubmitInfo SubmitInfo{};
	SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	SubmitInfo.waitSemaphoreCount = static_cast<std::uint32_t>(WaitVk.size());
	SubmitInfo.pWaitSemaphores = WaitVk.empty() ? nullptr : WaitVk.data();
	SubmitInfo.pWaitDstStageMask = WaitStages.empty() ? nullptr : WaitStages.data();
	SubmitInfo.commandBufferCount = static_cast<std::uint32_t>(VkBuffers.size());
	SubmitInfo.pCommandBuffers = VkBuffers.data();
	SubmitInfo.signalSemaphoreCount = static_cast<std::uint32_t>(SignalVk.size());
	SubmitInfo.pSignalSemaphores = SignalVk.empty() ? nullptr : SignalVk.data();

	VkFence FenceHandle = VK_NULL_HANDLE;
	if (SignalFence != nullptr)
	{
		FenceHandle = static_cast<FVulkanFence*>(SignalFence)->GetVkFence();
	}

	const VkResult Result = vkQueueSubmit(NativeQueue, 1, &SubmitInfo, FenceHandle);
	if (Result != VK_SUCCESS)
	{
		MAHO_CORE_ERROR("FVulkanQueue::Submit: vkQueueSubmit failed ({})", static_cast<int>(Result));
	}
}

FVulkanCommandList::~FVulkanCommandList()
{
	// Command buffers are freed with their pool by FVulkanRHI.
	Buffer = VK_NULL_HANDLE;
	Pool = VK_NULL_HANDLE;
}

void FVulkanCommandList::AssertType(ERHICommandListType Allowed) const
{
	assert(Type == Allowed && "FRHICommandList type mismatch");
	(void)Allowed;
}

void FVulkanCommandList::AssertNotTransfer() const
{
	assert(Type != ERHICommandListType::Transfer && "Command not valid on Transfer command list");
}

void FVulkanCommandList::Begin()
{
	VkCommandBufferBeginInfo BeginInfo{};
	BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkResetCommandBuffer(Buffer, 0);
	vkBeginCommandBuffer(Buffer, &BeginInfo);
	bRecording = true;
}

void FVulkanCommandList::End()
{
	vkEndCommandBuffer(Buffer);
	bRecording = false;
}

void FVulkanCommandList::CopyBuffer(FRHIBuffer* Src, std::uint64_t SrcOffset, FRHIBuffer* Dst, std::uint64_t DstOffset, std::uint64_t Size)
{
	auto* SrcVk = static_cast<FVulkanBuffer*>(Src);
	auto* DstVk = static_cast<FVulkanBuffer*>(Dst);
	if (SrcVk == nullptr || DstVk == nullptr)
	{
		return;
	}

	VkBufferCopy Region{};
	Region.srcOffset = SrcOffset;
	Region.dstOffset = DstOffset;
	Region.size = Size;
	vkCmdCopyBuffer(Buffer, SrcVk->GetVkBuffer(), DstVk->GetVkBuffer(), 1, &Region);
}

void FVulkanCommandList::CopyBufferToTexture(FRHIBuffer* /*Src*/, FRHITexture* /*Dst*/, std::uint64_t /*SrcOffset*/)
{
	// Skeleton: full image copy regions land in a follow-up.
}

void FVulkanCommandList::CopyTextureToBuffer(FRHITexture* /*Src*/, FRHIBuffer* /*Dst*/, std::uint64_t /*DstOffset*/)
{
}

void FVulkanCommandList::FillBuffer(FRHIBuffer* InBuffer, std::uint64_t Offset, std::uint64_t Size, std::uint32_t Data)
{
	auto* Buf = static_cast<FVulkanBuffer*>(InBuffer);
	if (Buf == nullptr)
	{
		return;
	}
	vkCmdFillBuffer(Buffer, Buf->GetVkBuffer(), Offset, Size, Data);
}

void FVulkanCommandList::TransitionBuffer(FRHIBuffer* InBuffer, ERHIResourceState OldState, ERHIResourceState NewState)
{
	auto* Buf = static_cast<FVulkanBuffer*>(InBuffer);
	if (Buf == nullptr)
	{
		return;
	}

	VkBufferMemoryBarrier Barrier{};
	Barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
	Barrier.srcAccessMask = ToVkAccess(OldState);
	Barrier.dstAccessMask = ToVkAccess(NewState);
	Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	Barrier.buffer = Buf->GetVkBuffer();
	Barrier.offset = 0;
	Barrier.size = VK_WHOLE_SIZE;

	vkCmdPipelineBarrier(
		Buffer,
		ToVkPipelineStage(OldState),
		ToVkPipelineStage(NewState),
		0,
		0, nullptr,
		1, &Barrier,
		0, nullptr);
}

void FVulkanCommandList::TransitionTexture(FRHITexture* /*Texture*/, ERHIResourceState /*OldState*/, ERHIResourceState /*NewState*/)
{
}

void FVulkanCommandList::BeginRenderPass()
{
	AssertType(ERHICommandListType::Graphics);
}

void FVulkanCommandList::EndRenderPass()
{
	AssertType(ERHICommandListType::Graphics);
}

void FVulkanCommandList::SetViewport(float X, float Y, float Width, float Height, float MinDepth, float MaxDepth)
{
	AssertType(ERHICommandListType::Graphics);
	VkViewport Viewport{};
	Viewport.x = X;
	Viewport.y = Y;
	Viewport.width = Width;
	Viewport.height = Height;
	Viewport.minDepth = MinDepth;
	Viewport.maxDepth = MaxDepth;
	vkCmdSetViewport(Buffer, 0, 1, &Viewport);
}

void FVulkanCommandList::SetScissor(std::int32_t X, std::int32_t Y, std::uint32_t Width, std::uint32_t Height)
{
	AssertType(ERHICommandListType::Graphics);
	VkRect2D Scissor{};
	Scissor.offset = { X, Y };
	Scissor.extent = { Width, Height };
	vkCmdSetScissor(Buffer, 0, 1, &Scissor);
}

void FVulkanCommandList::BindGraphicsPipeline(FRHIGraphicsPipeline* Pipeline)
{
	AssertType(ERHICommandListType::Graphics);
	auto* VkPipeline = static_cast<FVulkanGraphicsPipeline*>(Pipeline);
	if (VkPipeline == nullptr || VkPipeline->GetVkPipeline() == VK_NULL_HANDLE)
	{
		return;
	}
	vkCmdBindPipeline(Buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, VkPipeline->GetVkPipeline());
}

void FVulkanCommandList::BindVertexBuffer(std::uint32_t Binding, FRHIBuffer* InBuffer, std::uint64_t Offset)
{
	AssertType(ERHICommandListType::Graphics);
	auto* Buf = static_cast<FVulkanBuffer*>(InBuffer);
	if (Buf == nullptr)
	{
		return;
	}
	VkBuffer VkBuf = Buf->GetVkBuffer();
	vkCmdBindVertexBuffers(Buffer, Binding, 1, &VkBuf, &Offset);
}

void FVulkanCommandList::BindIndexBuffer(FRHIBuffer* InBuffer, std::uint64_t Offset, bool bIndex32)
{
	AssertType(ERHICommandListType::Graphics);
	auto* Buf = static_cast<FVulkanBuffer*>(InBuffer);
	if (Buf == nullptr)
	{
		return;
	}
	vkCmdBindIndexBuffer(
		Buffer,
		Buf->GetVkBuffer(),
		Offset,
		bIndex32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
}

void FVulkanCommandList::Draw(std::uint32_t VertexCount, std::uint32_t InstanceCount, std::uint32_t FirstVertex, std::uint32_t FirstInstance)
{
	AssertType(ERHICommandListType::Graphics);
	vkCmdDraw(Buffer, VertexCount, InstanceCount, FirstVertex, FirstInstance);
}

void FVulkanCommandList::DrawIndexed(
	std::uint32_t IndexCount,
	std::uint32_t InstanceCount,
	std::uint32_t FirstIndex,
	std::int32_t VertexOffset,
	std::uint32_t FirstInstance)
{
	AssertType(ERHICommandListType::Graphics);
	vkCmdDrawIndexed(Buffer, IndexCount, InstanceCount, FirstIndex, VertexOffset, FirstInstance);
}

void FVulkanCommandList::BindComputePipeline(FRHIComputePipeline* Pipeline)
{
	AssertType(ERHICommandListType::Compute);
	auto* VkPipeline = static_cast<FVulkanComputePipeline*>(Pipeline);
	if (VkPipeline == nullptr || VkPipeline->GetVkPipeline() == VK_NULL_HANDLE)
	{
		return;
	}
	vkCmdBindPipeline(Buffer, VK_PIPELINE_BIND_POINT_COMPUTE, VkPipeline->GetVkPipeline());
}

void FVulkanCommandList::Dispatch(std::uint32_t GroupCountX, std::uint32_t GroupCountY, std::uint32_t GroupCountZ)
{
	AssertType(ERHICommandListType::Compute);
	vkCmdDispatch(Buffer, GroupCountX, GroupCountY, GroupCountZ);
}

void FVulkanCommandList::BindDescriptorSets(std::uint32_t /*FirstSet*/, FRHIDescriptorSet* const* /*Sets*/, std::uint32_t /*Count*/)
{
	AssertNotTransfer();
}

void FVulkanCommandList::PushConstants(ERHIShaderStage /*Stages*/, std::uint32_t /*Offset*/, std::uint32_t /*Size*/, const void* /*Data*/)
{
	AssertNotTransfer();
}

} // namespace Maho
