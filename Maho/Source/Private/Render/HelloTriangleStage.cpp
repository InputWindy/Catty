#include "MahoStage_HelloTriangle.h"

#include <Core/System/Log.h>
#include <Render/MahoCommonUniforms.h>
#include <Render/RenderServer.h>
#include <Render/ShaderCache.h>
#include <Render/ShaderLoader.h>

#include "Render/RHI/VulkanRHI.h"
#include "Render/RHI/VulkanResources.h"

namespace Maho
{

namespace
{

struct FSimpleVertex
{
	float Pos[3];
	float Col[3];
};

static const FSimpleVertex TriangleVertices[] =
{
	{ {  0.0f,  0.5f, 0.0f },  { 1.0f, 0.0f, 0.0f } },
	{ {  0.5f, -0.5f, 0.0f },  { 0.0f, 1.0f, 0.0f } },
	{ { -0.5f, -0.5f, 0.0f },  { 0.0f, 0.0f, 1.0f } },
};

static void UploadBufferToGPU(IRHI* RHI, FRHIBuffer* Dst, const void* Data, std::uint64_t Size)
{
	FRHIBufferDesc StageDesc;
	StageDesc.Size = Size;
	StageDesc.Usage = ERHIBufferUsage::TransferSrc;
	StageDesc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
	FRHIBuffer* Staging = RHI->CreateBuffer(StageDesc);
	if (Staging == nullptr)
	{
		return;
	}

	RHI->UpdateBuffer(Staging, 0, Size, Data);

	FRHICommandList* Cmd = RHI->CreateCommandList(ERHICommandListType::Graphics);
	Cmd->Begin();
	Cmd->CopyBuffer(Staging, 0, Dst, 0, Size);
	Cmd->End();
	RHI->GetGraphicsQueue().Submit(&Cmd, 1, nullptr, 0, nullptr, 0, nullptr);
	RHI->DestroyCommandList(Cmd);
	RHI->DestroyBuffer(Staging);
}

} // namespace

FHelloTriangleStage::FHelloTriangleStage()
	: FRenderExtension("HelloTriangle")
	, Ptr(std::make_unique<FImpl>())
{
}

FHelloTriangleStage::~FHelloTriangleStage() = default;

bool FHelloTriangleStage::ExecuteStage(ERenderStage Stage, FRenderServer& RenderServer)
{
	switch (Stage)
	{
	case ERenderStage::BeginFrame:
		return OnBeginFrame(RenderServer);
	case ERenderStage::KickRHI:
		return OnKickRHI(RenderServer);
	default:
		return true;
	}
}

bool FHelloTriangleStage::Initialize(FRenderServer& RenderServer)
{
	auto& S = *Ptr;
	S.RHI = RenderServer.GetRHIServer().GetRHI();
	S.VkRHI = RenderServer.GetVulkanRHI();
	if (S.RHI == nullptr || S.VkRHI == nullptr)
	{
		return false;
	}

	VkDevice Device = S.VkRHI->GetVkDevice();

	if (!FShaderCompiler::Initialize())
	{
		MAHO_CORE_ERROR("HelloTriangle: shader compiler init failed");
		return false;
	}

	S.ShaderCache = std::make_unique<FShaderCache>("Cached");
	S.ShaderLoader = std::make_unique<FShaderLoader>(*S.ShaderCache,
		std::vector<std::string>{ "Engine/Shaders", "Project/Shaders" },
		std::vector<std::string>{ "Engine/Shaders/Common", "Project/Shaders/Common" });

	FShaderPackage Package = S.ShaderLoader->LoadShader("Triangle.shader");
	if (!Package.Vertex.bSuccess || !Package.Fragment.bSuccess)
	{
		MAHO_CORE_ERROR("HelloTriangle: shader load failed\nVS: {}\nFS: {}",
		                Package.Vertex.ErrorLog, Package.Fragment.ErrorLog);
		return false;
	}

	// Shader modules
	{
		FRHIShaderModuleDesc VsDesc;
		VsDesc.Stage = ERHIShaderStage::Vertex;
		VsDesc.Bytecode = Package.Vertex.Bytecode.data();
		VsDesc.BytecodeSize = Package.Vertex.Bytecode.size() * sizeof(std::uint32_t);
		S.VertexShader = S.RHI->CreateShaderModule(VsDesc);

		FRHIShaderModuleDesc FsDesc;
		FsDesc.Stage = ERHIShaderStage::Fragment;
		FsDesc.Bytecode = Package.Fragment.Bytecode.data();
		FsDesc.BytecodeSize = Package.Fragment.Bytecode.size() * sizeof(std::uint32_t);
		S.FragmentShader = S.RHI->CreateShaderModule(FsDesc);
	}

	if (S.VertexShader == nullptr || S.FragmentShader == nullptr)
	{
		MAHO_CORE_ERROR("HelloTriangle: shader module creation failed");
		return false;
	}

	// VBO
	{
		FRHIBufferDesc Desc;
		Desc.Size = sizeof(TriangleVertices);
		Desc.Usage = ERHIBufferUsage::Vertex;
		Desc.MemoryUsage = ERHIMemoryUsage::GPUOnly;
		S.TriangleVBO = S.RHI->CreateBuffer(Desc);
		UploadBufferToGPU(S.RHI, S.TriangleVBO, TriangleVertices, sizeof(TriangleVertices));
	}

	// Uniform buffers
	{
		FRHIBufferDesc Desc;
		Desc.Size = sizeof(FFrameUniforms);
		Desc.Usage = ERHIBufferUsage::Uniform;
		Desc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
		S.FrameUniformBuf = S.RHI->CreateBuffer(Desc);
	}
	{
		FRHIBufferDesc Desc;
		Desc.Size = sizeof(FObjectUniforms);
		Desc.Usage = ERHIBufferUsage::Uniform;
		Desc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
		S.ObjectUniformBuf = S.RHI->CreateBuffer(Desc);
	}

	// Descriptor set layouts
	{
		FRHIDescriptorSetLayoutDesc Desc;
		FRHIDescriptorBinding B;
		B.Binding = 0;
		B.Type = ERHIDescriptorType::UniformBuffer;
		B.Count = 1;
		B.Stages = ERHIShaderStage::AllGraphics;
		Desc.Bindings.push_back(B);
		S.FrameSetLayout = S.RHI->CreateDescriptorSetLayout(Desc);
		S.ObjectSetLayout = S.RHI->CreateDescriptorSetLayout(Desc);
	}

	// Pipeline layout
	{
		FRHIPipelineLayoutDesc Desc;
		Desc.SetLayouts = { S.FrameSetLayout, S.ObjectSetLayout };
		S.PipelineLayout = S.RHI->CreatePipelineLayout(Desc);
	}

	// Offscreen render pass
	{
		FRHIRenderPassDesc Desc;
		FRHIRenderPassAttachment Att;
		Att.Format = ERHIFormat::B8G8R8A8_UNORM;
		Att.LoadOp = ERHILoadOp::Clear;
		Att.StoreOp = ERHIStoreOp::Store;
		Desc.ColorAttachments.push_back(Att);
		S.OffscreenPass = S.RHI->CreateRenderPass(Desc);
	}

	// Offscreen texture + view + framebuffer
	{
		FRHITextureDesc Desc;
		Desc.Format = ERHIFormat::B8G8R8A8_UNORM;
		Desc.Dimension = ERHITextureDimension::Tex2D;
		Desc.Extent = { S.VpWidth, S.VpHeight, 1 };
		Desc.Usage = ERHITextureUsage::ColorAttachment | ERHITextureUsage::Sampled;
		S.ViewportTex = S.RHI->CreateTexture(Desc);

		FRHITextureViewDesc ViewDesc;
		ViewDesc.Texture = S.ViewportTex;
		ViewDesc.Format = ERHIFormat::B8G8R8A8_UNORM;
		S.ViewportTexView = S.RHI->CreateTextureView(ViewDesc);
	}
	{
		FRHIFramebufferDesc Desc;
		Desc.RenderPass = S.OffscreenPass;
		Desc.Attachments = { S.ViewportTexView };
		Desc.Width = S.VpWidth;
		Desc.Height = S.VpHeight;
		S.OffscreenFB = S.RHI->CreateFramebuffer(Desc);
	}

	// Graphics pipeline
	{
		FRHIGraphicsPipelineDesc Desc;
		Desc.VertexShader = S.VertexShader;
		Desc.FragmentShader = S.FragmentShader;
		Desc.Layout = S.PipelineLayout;
		Desc.RenderPass = S.OffscreenPass;
		Desc.Topology = ERHIPrimitiveTopology::TriangleList;
		Desc.VertexStride = sizeof(FSimpleVertex);

		FRHIVertexAttribute Pos;
		Pos.Location = 0;
		Pos.Format = ERHIFormat::R32G32B32_SFLOAT;
		Pos.Offset = 0;
		Desc.Attributes.push_back(Pos);

		FRHIVertexAttribute Col;
		Col.Location = 1;
		Col.Format = ERHIFormat::R32G32B32_SFLOAT;
		Col.Offset = 12;
		Desc.Attributes.push_back(Col);

		Desc.CullMode = ERHICullMode::None;
		Desc.ColorFormat = ERHIFormat::B8G8R8A8_UNORM;

		FRHIAttachmentBlend Blend;
		Desc.AttachmentBlends.push_back(Blend);

		S.Pipeline = S.RHI->CreateGraphicsPipeline(Desc);
	}

	// Descriptor pool + sets
	{
		FRHIDescriptorPoolDesc Desc;
		Desc.MaxSets = 2;
		FRHIDescriptorPoolSize Sz;
		Sz.Type = ERHIDescriptorType::UniformBuffer;
		Sz.Count = 2;
		Desc.PoolSizes.push_back(Sz);
		S.DescPool = S.RHI->CreateDescriptorPool(Desc);
	}
	S.FrameDescSet = S.RHI->AllocateDescriptorSet(S.DescPool, S.FrameSetLayout);
	S.ObjectDescSet = S.RHI->AllocateDescriptorSet(S.DescPool, S.ObjectSetLayout);

	{
		FRHIDescriptorWrite W{};
		W.Set = S.FrameDescSet;
		W.Binding = 0;
		W.Type = ERHIDescriptorType::UniformBuffer;
		W.Buffer = S.FrameUniformBuf;
		W.Range = sizeof(FFrameUniforms);
		S.RHI->UpdateDescriptorSets(&W, 1);
	}
	{
		FRHIDescriptorWrite W{};
		W.Set = S.ObjectDescSet;
		W.Binding = 0;
		W.Type = ERHIDescriptorType::UniformBuffer;
		W.Buffer = S.ObjectUniformBuf;
		W.Range = sizeof(FObjectUniforms);
		S.RHI->UpdateDescriptorSets(&W, 1);
	}

	// Offscreen command pool
	{
		VkCommandPoolCreateInfo PoolInfo{};
		PoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		PoolInfo.queueFamilyIndex = S.VkRHI->GetGraphicsQueueFamilyIndex();
		PoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	VkCommandPool Pool = VK_NULL_HANDLE;
	if (vkCreateCommandPool(Device, &PoolInfo, nullptr, &Pool) != VK_SUCCESS)
	{
		MAHO_CORE_ERROR("HelloTriangle: offscreen command pool creation failed");
		return false;
	}
	S.OffscreenCmdPoolHandle = reinterpret_cast<void*>(Pool);
	}

	MAHO_CORE_INFO("HelloTriangle: initialized ({}x{})", S.VpWidth, S.VpHeight);
	return true;
}

bool FHelloTriangleStage::OnBeginFrame(FRenderServer& RenderServer)
{
	if (!Ptr->bInitialized)
	{
		Ptr->bInitialized = Initialize(RenderServer);
	}
	return true;
}

bool FHelloTriangleStage::OnKickRHI(FRenderServer& RenderServer)
{
	auto& S = *Ptr;
	if (!S.bInitialized)
	{
		return true;
	}

	VkDevice Device = S.VkRHI->GetVkDevice();
	VkCommandPool CmdPool = reinterpret_cast<VkCommandPool>(reinterpret_cast<std::uintptr_t>(S.OffscreenCmdPoolHandle));

	// Fill uniforms
	{
		FFrameUniforms Uni{};
		Uni.View[0] = Uni.View[5] = Uni.View[10] = Uni.View[15] = 1.0f;
		Uni.Proj[0] = Uni.Proj[5] = Uni.Proj[10] = Uni.Proj[15] = 1.0f;
		Uni.ViewProj[0] = Uni.ViewProj[5] = Uni.ViewProj[10] = Uni.ViewProj[15] = 1.0f;
		S.RHI->UpdateBuffer(S.FrameUniformBuf, 0, sizeof(FFrameUniforms), &Uni);
	}
	{
		FObjectUniforms Uni{};
		Uni.LocalToWorld[0] = Uni.LocalToWorld[5] = Uni.LocalToWorld[10] = Uni.LocalToWorld[15] = 1.0f;
		Uni.LocalToWorldInverseTranspose[0] = Uni.LocalToWorldInverseTranspose[5] =
			Uni.LocalToWorldInverseTranspose[10] = Uni.LocalToWorldInverseTranspose[15] = 1.0f;
		S.RHI->UpdateBuffer(S.ObjectUniformBuf, 0, sizeof(FObjectUniforms), &Uni);
	}

	// Draw to offscreen texture
	auto* VkPipe = static_cast<FVulkanGraphicsPipeline*>(S.Pipeline);
	auto* VkLayout = static_cast<FVulkanPipelineLayout*>(S.PipelineLayout);
	auto* VkPass = static_cast<FVulkanRenderPass*>(S.OffscreenPass);
	auto* VkFB = static_cast<FVulkanFramebuffer*>(S.OffscreenFB);
	auto* VkVBO = static_cast<FVulkanBuffer*>(S.TriangleVBO);
	auto* VkDescSet0 = static_cast<FVulkanDescriptorSet*>(S.FrameDescSet);
	auto* VkDescSet1 = static_cast<FVulkanDescriptorSet*>(S.ObjectDescSet);
	auto* VkTex = static_cast<FVulkanTexture*>(S.ViewportTex);

	VkCommandBuffer CmdBuf = VK_NULL_HANDLE;
	{
		VkCommandBufferAllocateInfo AllocInfo{};
		AllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		AllocInfo.commandPool = CmdPool;
		AllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		AllocInfo.commandBufferCount = 1;
		if (vkAllocateCommandBuffers(Device, &AllocInfo, &CmdBuf) != VK_SUCCESS)
		{
			return true;
		}
	}

	VkCommandBufferBeginInfo BeginInfo{};
	BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	vkBeginCommandBuffer(CmdBuf, &BeginInfo);

	// Transition to color attachment
	{
		VkImageMemoryBarrier Barrier{};
		Barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		Barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		Barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		Barrier.image = VkTex->GetVkImage();
		Barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		Barrier.subresourceRange.baseMipLevel = 0;
		Barrier.subresourceRange.levelCount = 1;
		Barrier.subresourceRange.baseArrayLayer = 0;
		Barrier.subresourceRange.layerCount = 1;
		Barrier.srcAccessMask = 0;
		Barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		vkCmdPipelineBarrier(CmdBuf,
		                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		                     0, 0, nullptr, 0, nullptr, 1, &Barrier);
	}

	// Begin render pass
	{
		VkRenderPassBeginInfo RpInfo{};
		RpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
		RpInfo.renderPass = VkPass->GetVkPass();
		RpInfo.framebuffer = VkFB->GetVkFramebuffer();
		RpInfo.renderArea.extent = { S.VpWidth, S.VpHeight };
		VkClearValue ClearVal{};
		ClearVal.color = { { 0.08f, 0.10f, 0.16f, 1.0f } };
		RpInfo.clearValueCount = 1;
		RpInfo.pClearValues = &ClearVal;
		vkCmdBeginRenderPass(CmdBuf, &RpInfo, VK_SUBPASS_CONTENTS_INLINE);
	}

	vkCmdBindPipeline(CmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, VkPipe->GetVkPipeline());

	{
		VkBuffer Vb = VkVBO->GetVkBuffer();
		VkDeviceSize Off = 0;
		vkCmdBindVertexBuffers(CmdBuf, 0, 1, &Vb, &Off);
	}

	{
		VkDescriptorSet Sets[] = { VkDescSet0->GetVkSet(), VkDescSet1->GetVkSet() };
		vkCmdBindDescriptorSets(CmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
			VkLayout->GetVkLayout(), 0, 2, Sets, 0, nullptr);
	}

	{
		VkViewport Vp{};
		Vp.x = 0.0f;
		Vp.y = 0.0f;
		Vp.width = static_cast<float>(S.VpWidth);
		Vp.height = static_cast<float>(S.VpHeight);
		Vp.minDepth = 0.0f;
		Vp.maxDepth = 1.0f;
		vkCmdSetViewport(CmdBuf, 0, 1, &Vp);

		VkRect2D Sc{};
		Sc.extent = { S.VpWidth, S.VpHeight };
		vkCmdSetScissor(CmdBuf, 0, 1, &Sc);
	}

	vkCmdDraw(CmdBuf, 3, 1, 0, 0);
	vkCmdEndRenderPass(CmdBuf);

	// Transition to shader read
	{
		VkImageMemoryBarrier Barrier{};
		Barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		Barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		Barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		Barrier.image = VkTex->GetVkImage();
		Barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		Barrier.subresourceRange.baseMipLevel = 0;
		Barrier.subresourceRange.levelCount = 1;
		Barrier.subresourceRange.baseArrayLayer = 0;
		Barrier.subresourceRange.layerCount = 1;
		Barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

		vkCmdPipelineBarrier(CmdBuf,
		                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
		                     0, 0, nullptr, 0, nullptr, 1, &Barrier);
	}

	vkEndCommandBuffer(CmdBuf);

	{
		VkSubmitInfo SubmitInfo{};
		SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		SubmitInfo.commandBufferCount = 1;
		SubmitInfo.pCommandBuffers = &CmdBuf;
		vkQueueSubmit(S.VkRHI->GetVkGraphicsQueue(), 1, &SubmitInfo, VK_NULL_HANDLE);
	}

	return true;
}

void RegisterHelloTriangleStage(FRenderServer& Server)
{
	Server.RegisterRenderExtension<FHelloTriangleStage>();
}

} // namespace Maho
