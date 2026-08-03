#pragma once

#include <Core/Export.h>
#include <Render/RHI/RHIEnums.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Maho
{

struct FShaderReflection;

class MAHO_API FShaderCache
{
public:
	explicit FShaderCache(std::string CacheDir);

	bool TryLoad(const std::string& Key, std::vector<std::uint32_t>& OutBytecode);
	bool TryLoadReflection(const std::string& Key, FShaderReflection& OutReflection);
	void Store(const std::string& Key,
	           const std::vector<std::uint32_t>& Bytecode,
	           const FShaderReflection& Reflection);

	static std::string MakeKey(const std::string& Path,
	                           ERHIShaderStage Stage,
	                           const std::string& EntryPoint,
	                           const std::vector<std::string>& Defines);

private:
	std::string CacheRoot;  // {CachedDir}/Shaders/
};

} // namespace Maho
