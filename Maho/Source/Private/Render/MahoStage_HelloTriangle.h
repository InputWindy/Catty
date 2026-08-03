#pragma once

#include <Render/Sequencer/RenderExtension.h>

#include <cstdint>
#include <memory>

namespace Maho
{

class FRenderServer;
class IRHI;
class FVulkanRHI;
class FShaderCache;
class FShaderLoader;

class FRHIBuffer;
class FRHIShaderModule;
class FRHIDescriptorSetLayout;
class FRHIPipelineLayout;
class FRHIRenderPass;
class FRHIFramebuffer;
class FRHIGraphicsPipeline;
class FRHITexture;
class FRHITextureView;
class FRHIDescriptorPool;
class FRHIDescriptorSet;

// Demo render extension: loads Triangle.shader, compiles GLSL->SPIR-V,
// creates a full graphics pipeline, and renders to an offscreen texture.
class FHelloTriangleStage final : public FRenderExtension
{
public:
	FHelloTriangleStage();
	~FHelloTriangleStage() override;

	bool ExecuteStage(ERenderStage Stage, FRenderServer& RenderServer) override;

private:
	struct FImpl
	{
		bool bInitialized = false;

		IRHI* RHI = nullptr;
		FVulkanRHI* VkRHI = nullptr;

		FRHIShaderModule* VertexShader = nullptr;
		FRHIShaderModule* FragmentShader = nullptr;
		FRHIDescriptorSetLayout* FrameSetLayout = nullptr;
		FRHIDescriptorSetLayout* ObjectSetLayout = nullptr;
		FRHIPipelineLayout* PipelineLayout = nullptr;
		FRHIRenderPass* OffscreenPass = nullptr;
		FRHIFramebuffer* OffscreenFB = nullptr;
		FRHIGraphicsPipeline* Pipeline = nullptr;
		FRHIBuffer* TriangleVBO = nullptr;
		FRHIBuffer* FrameUniformBuf = nullptr;
		FRHIBuffer* ObjectUniformBuf = nullptr;
		FRHITexture* ViewportTex = nullptr;
		FRHITextureView* ViewportTexView = nullptr;
		FRHIDescriptorPool* DescPool = nullptr;
		FRHIDescriptorSet* FrameDescSet = nullptr;
		FRHIDescriptorSet* ObjectDescSet = nullptr;

		void* OffscreenCmdPoolHandle = nullptr;
		std::unique_ptr<FShaderCache> ShaderCache;
		std::unique_ptr<FShaderLoader> ShaderLoader;

		std::uint32_t VpWidth = 600;
		std::uint32_t VpHeight = 400;
	};

	std::unique_ptr<FImpl> Ptr;

	bool Initialize(FRenderServer& RenderServer);
	bool OnBeginFrame(FRenderServer& RenderServer);
	bool OnKickRHI(FRenderServer& RenderServer);
};

// Create and register the HelloTriangle render extension.
void RegisterHelloTriangleStage(FRenderServer& Server);

} // namespace Maho
