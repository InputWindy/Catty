#include <Render/ShaderLoader.h>

#include <Core/System/Log.h>
#include <Core/System/Paths.h>
#include <Render/ShaderCache.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace Maho
{

FShaderLoader::FShaderLoader(FShaderCache& Cache,
                             std::vector<std::string> SearchPaths,
                             std::vector<std::string> IncludePaths)
	: CachePtr(&Cache)
	, ShaderPaths(std::move(SearchPaths))
	, IncludePaths(std::move(IncludePaths))
{
}

std::string FShaderLoader::ReadFile(const std::string& Path)
{
	std::ifstream File(Path, std::ios::binary);
	if (!File)
	{
		return {};
	}
	std::ostringstream Ss;
	Ss << File.rdbuf();
	return Ss.str();
}

static void TrimInline(std::string& S)
{
	// Remove leading/trailing whitespace and carriage returns.
	while (!S.empty() && std::isspace(static_cast<unsigned char>(S.back())))
	{
		S.pop_back();
	}
	if (!S.empty() && S.front() == '\r')
	{
		S.erase(0, 1);
	}
}

static bool StartsWith(const std::string& S, const char* Prefix)
{
	std::size_t Len = std::strlen(Prefix);
	return (S.size() >= Len) && (S.compare(0, Len, Prefix) == 0);
}

std::string FShaderLoader::PreprocessIncludes(const std::string& Source)
{
	std::istringstream In(Source);
	std::ostringstream Out;
	std::string Line;

	while (std::getline(In, Line))
	{
		TrimInline(Line);

		if (StartsWith(Line, "#include"))
		{
			auto Start = Line.find('"');
			auto End = Line.rfind('"');
			if (Start != std::string::npos && End != std::string::npos && End > Start)
			{
				std::string IncludeFile = Line.substr(Start + 1, End - Start - 1);
				std::string Resolved = ResolveInclude(IncludeFile);
				if (!Resolved.empty())
				{
					std::string IncludedSource = ReadFile(Resolved);
					if (!IncludedSource.empty())
					{
						Out << "// --- begin include: " << IncludeFile << " ---\n";
						Out << IncludedSource;
						Out << "// --- end include: " << IncludeFile << " ---\n";
						continue;
					}
				}
			}
		}

		Out << Line << '\n';
	}

	return Out.str();
}

std::string FShaderLoader::ResolveInclude(const std::string& IncludeName)
{
	for (const auto& Dir : IncludePaths)
	{
		std::string Candidate = Dir;
		if (!Candidate.empty() && Candidate.back() != '/' && Candidate.back() != '\\')
		{
			Candidate += '/';
		}
		Candidate += IncludeName;

		std::ifstream Test(Candidate);
		if (Test.good())
		{
			return Candidate;
		}
	}
	return {};
}

std::string FShaderLoader::LocateFile(const std::string& RelativePath)
{
	for (const auto& Dir : ShaderPaths)
	{
		std::string Candidate = Dir;
		if (!Candidate.empty() && Candidate.back() != '/' && Candidate.back() != '\\')
		{
			Candidate += '/';
		}
		Candidate += RelativePath;

		std::ifstream Test(Candidate);
		if (Test.good())
		{
			return Candidate;
		}
	}
	return {};
}

void FShaderLoader::ParseShaderSource(const std::string& RawSource, FShaderCompileDesc& OutDesc)
{
	OutDesc.Source = RawSource;

	std::istringstream In(RawSource);
	std::string Line;

	while (std::getline(In, Line))
	{
		TrimInline(Line);

		if (StartsWith(Line, "#pragma vertex"))
		{
			auto Pos = Line.find_first_not_of(" \t", 14); // len("#pragma vertex") == 14
			if (Pos != std::string::npos)
			{
				OutDesc.VertexEntry = Line.substr(Pos);
			}
		}
		else if (StartsWith(Line, "#pragma fragment"))
		{
			auto Pos = Line.find_first_not_of(" \t", 16); // len("#pragma fragment") == 16
			if (Pos != std::string::npos)
			{
				OutDesc.FragmentEntry = Line.substr(Pos);
			}
		}
	}
}

FShaderPackage FShaderLoader::LoadShader(const std::string& ShaderPath)
{
	FShaderPackage Package;

	std::string FullPath = LocateFile(ShaderPath);
	if (FullPath.empty())
	{
		MAHO_CORE_ERROR("FShaderLoader: shader not found: {}", ShaderPath);
		return Package;
	}

	std::string RawSource = ReadFile(FullPath);
	if (RawSource.empty())
	{
		MAHO_CORE_ERROR("FShaderLoader: failed to read shader: {}", FullPath);
		return Package;
	}

	// Preprocess #include directives
	std::string ProcessedSource = PreprocessIncludes(RawSource);

	FShaderCompileDesc Desc;
	ParseShaderSource(ProcessedSource, Desc);

	if (Desc.VertexEntry.empty() || Desc.FragmentEntry.empty())
	{
		MAHO_CORE_ERROR("FShaderLoader: missing #pragma vertex/fragment in shader: {}", ShaderPath);
		return Package;
	}

	// Compile vertex stage
	std::string VtxKey = FShaderCache::MakeKey(ShaderPath, ERHIShaderStage::Vertex,
	                                           Desc.VertexEntry, Desc.Defines);
	if (!CachePtr->TryLoad(VtxKey, Package.Vertex.Bytecode))
	{
		Package.Vertex = FShaderCompiler::CompileStage(Desc, ERHIShaderStage::Vertex, Desc.VertexEntry);
		if (Package.Vertex.bSuccess)
		{
			CachePtr->Store(VtxKey, Package.Vertex.Bytecode, Package.Vertex.Reflection);
		}
		else
		{
			MAHO_CORE_ERROR("FShaderLoader: vertex compilation failed for {}: {}",
			                ShaderPath, Package.Vertex.ErrorLog);
			return Package;
		}
	}
	else
	{
		Package.Vertex.bSuccess = true;
		CachePtr->TryLoadReflection(VtxKey, Package.Vertex.Reflection);
		MAHO_CORE_INFO("FShaderLoader: vertex cache hit for {}", ShaderPath);
	}

	// Compile fragment stage
	std::string FragKey = FShaderCache::MakeKey(ShaderPath, ERHIShaderStage::Fragment,
	                                            Desc.FragmentEntry, Desc.Defines);
	if (!CachePtr->TryLoad(FragKey, Package.Fragment.Bytecode))
	{
		Package.Fragment = FShaderCompiler::CompileStage(Desc, ERHIShaderStage::Fragment, Desc.FragmentEntry);
		if (Package.Fragment.bSuccess)
		{
			CachePtr->Store(FragKey, Package.Fragment.Bytecode, Package.Fragment.Reflection);
		}
		else
		{
			MAHO_CORE_ERROR("FShaderLoader: fragment compilation failed for {}: {}",
			                ShaderPath, Package.Fragment.ErrorLog);
			return Package;
		}
	}
	else
	{
		Package.Fragment.bSuccess = true;
		CachePtr->TryLoadReflection(FragKey, Package.Fragment.Reflection);
		MAHO_CORE_INFO("FShaderLoader: fragment cache hit for {}", ShaderPath);
	}

	MAHO_CORE_INFO("FShaderLoader: loaded shader={}", ShaderPath);
	return Package;
}

FRHIDescriptorSetLayoutDesc BuildDescriptorSetLayoutFromReflection(
	const FShaderReflection& Reflection)
{
	FRHIDescriptorSetLayoutDesc Desc;

	// Collect unique binding descriptors from uniform blocks and samplers.
	struct FBindingKey
	{
		std::uint32_t Set;
		std::uint32_t Binding;
		bool operator<(const FBindingKey& Other) const
		{
			if (Set != Other.Set) return Set < Other.Set;
			return Binding < Other.Binding;
		}
	};

	std::map<FBindingKey, FRHIDescriptorBinding> BindingMap;

	for (const auto& Block : Reflection.UniformBlocks)
	{
		FBindingKey Key{Block.Set, Block.Binding};
		FRHIDescriptorBinding& Bind = BindingMap[Key];
		Bind.Binding = Block.Binding;
		Bind.Type = ERHIDescriptorType::UniformBuffer;
		Bind.Count = 1;
		Bind.Stages = Block.Stages;
	}

	for (const auto& Sam : Reflection.Samplers)
	{
		FBindingKey Key{Sam.Set, Sam.Binding};
		FRHIDescriptorBinding& Bind = BindingMap[Key];
		Bind.Binding = Sam.Binding;
		Bind.Type = ERHIDescriptorType::CombinedImageSampler;
		Bind.Count = 1;
		Bind.Stages = Sam.Stages;
	}

	for (const auto& [Key, Bind] : BindingMap)
	{
		Desc.Bindings.push_back(Bind);
	}

	return Desc;
}

FRHIPipelineLayoutDesc BuildPipelineLayoutFromReflection(
	const FShaderReflection& VertexReflection,
	const FShaderReflection& FragmentReflection)
{
	FRHIPipelineLayoutDesc Desc;
	// For now we don't auto-create descriptor set layouts and pipeline layouts —
	// they need actual FRHI objects. The caller should use these desc structs.
	// This utility just merges the reflection data into pipeline layout descriptor form.
	// TODO: future — actual CreateDescriptorSetLayout / CreatePipelineLayout in IRHI.

	// Collect push constants (union of vertex + fragment stages)
	std::vector<FRHIPushConstantRange> PcRanges;
	PcRanges.insert(PcRanges.end(),
	                VertexReflection.PushConstants.begin(),
	                VertexReflection.PushConstants.end());
	PcRanges.insert(PcRanges.end(),
	                FragmentReflection.PushConstants.begin(),
	                FragmentReflection.PushConstants.end());
	Desc.PushConstants = std::move(PcRanges);

	return Desc;
}

} // namespace Maho
