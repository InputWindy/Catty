#pragma once

#include <Core/Export.h>
#include <Render/RHI/RHIEnums.h>
#include <Render/RHI/RHIResources.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Maho
{

struct FShaderParamInfo
{
	std::string Name;
	std::uint32_t Offset = 0;
	std::uint32_t Size = 0;
	ERHIDescriptorType Type = ERHIDescriptorType::UniformBuffer;
	std::uint32_t Set = 0;
	std::uint32_t Binding = 0;
};

struct FShaderUniformBlockInfo
{
	std::string BlockName;
	std::uint32_t Set = 0;
	std::uint32_t Binding = 0;
	std::uint32_t BlockSize = 0;
	ERHIShaderStage Stages = ERHIShaderStage::None;
	std::vector<FShaderParamInfo> Members;
};

struct FShaderSamplerInfo
{
	std::string Name;
	std::uint32_t Set = 0;
	std::uint32_t Binding = 0;
	ERHIShaderStage Stages = ERHIShaderStage::None;
};

struct FShaderReflection
{
	std::vector<FShaderUniformBlockInfo> UniformBlocks;
	std::vector<FShaderSamplerInfo> Samplers;
	std::vector<FRHIPushConstantRange> PushConstants;
};

struct FShaderCompileDesc
{
	std::string Source;                    // Entire .shader file GLSL source
	std::string VertexEntry;               // #pragma vertex 入口函数名
	std::string FragmentEntry;             // #pragma fragment 入口函数名
	std::vector<std::string> Defines;      // 宏定义（未来 multi_compile 用）
};

struct FShaderCompileResult
{
	bool bSuccess = false;
	std::vector<std::uint32_t> Bytecode;   // SPIR-V
	FShaderReflection Reflection;
	std::string ErrorLog;
};

class MAHO_API FShaderCompiler
{
public:
	static bool Initialize();
	static void Shutdown();

	// Compile a single stage (Vertex or Fragment) from shared source.
	// Same Source text; switch EShLang + entry point per compile call.
	static FShaderCompileResult CompileStage(
		const FShaderCompileDesc& Desc,
		ERHIShaderStage Stage,
		const std::string& EntryPoint);

private:
	static bool bInitialized;
};

} // namespace Maho
