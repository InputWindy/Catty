#pragma once

#include <Core/Export.h>
#include <Render/ShaderCompiler.h>

#include <string>
#include <vector>

namespace Maho
{

class FShaderCache;

// Compiled SPIR-V + reflection for all stages from one .shader file.
struct FShaderPackage
{
	FShaderCompileResult Vertex;
	FShaderCompileResult Fragment;
};

class MAHO_API FShaderLoader
{
public:
	explicit FShaderLoader(FShaderCache& Cache,
	                       std::vector<std::string> SearchPaths,
	                       std::vector<std::string> IncludePaths);

	// Load a .shader file, produces compiled results for all stages.
	FShaderPackage LoadShader(const std::string& ShaderPath);

private:
	FShaderCache* CachePtr;

	// Search paths: {EngineShadersDir}, {ProjectShadersDir}
	std::vector<std::string> ShaderPaths;

	// #include search paths: {EngineShadersDir}/Common/, {ProjectShadersDir}/Common/
	std::vector<std::string> IncludePaths;

	// Parse #pragma vertex <name> / #pragma fragment <name>
	void ParseShaderSource(const std::string& RawSource, FShaderCompileDesc& OutDesc);

	std::string LocateFile(const std::string& RelativePath);
	std::string ResolveInclude(const std::string& IncludeName);
	std::string ReadFile(const std::string& Path);
	std::string PreprocessIncludes(const std::string& Source);
};

// Utility: build DescriptorSetLayoutDesc from reflection data.
FRHIDescriptorSetLayoutDesc BuildDescriptorSetLayoutFromReflection(
	const FShaderReflection& Reflection);

// Utility: merge VS + FS reflection into a PipelineLayoutDesc.
FRHIPipelineLayoutDesc BuildPipelineLayoutFromReflection(
	const FShaderReflection& VertexReflection,
	const FShaderReflection& FragmentReflection);

} // namespace Maho
