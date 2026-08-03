#pragma once

#include <Core/Export.h>
#include <Render/RDG/RDGPass.h>
#include <Render/RDG/RDGResources.h>
#include <Render/RDG/RDGTransientPool.h>
#include <Render/RHI/RHICommandList.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace Maho
{

class IRHI;
class FRDGBuffer;
class FRDGTexture;

/**
 * Render Graph Builder — single-frame pass graph compiler + executor.
 *
 * Usage (inside IRenderFeature::BuildRenderGraph):
 *   1. RegisterExternalBuffer/Texture for persistent resources.
 *   2. CreateBuffer/Texture for transient frame-lifetime resources.
 *   3. AddRasterPass / AddComputePass to declare GPU work.
 *   4. Read / Write to declare resource access states.
 *   5. Export resources for cross-feature sharing within the same stage.
 *   6. FRenderServer calls Compile() then Execute().
 */
class MAHO_API FRDGBuilder
{
public:
	explicit FRDGBuilder(IRHI* InRHI);
	~FRDGBuilder();
	FRDGBuilder(const FRDGBuilder&) = delete;
	FRDGBuilder& operator=(const FRDGBuilder&) = delete;

	// ── Resource registration ──

	FRDGBuffer* RegisterExternalBuffer(FRHIBuffer* Buffer,
	                                   ERHIResourceState InitialState,
	                                   const char* Name);
	FRDGTexture* RegisterExternalTexture(FRHITexture* Texture,
	                                     ERHIResourceState InitialState,
	                                     const char* Name);

	// ── Transient resource creation ──

	FRDGBuffer* CreateBuffer(const FRHIBufferDesc& Desc, const char* Name);
	FRDGTexture* CreateTexture(const FRHITextureDesc& Desc, const char* Name);

	// ── Cross-feature resource exchange (same stage only) ──

	void Export(FRDGResource* Resource, const char* Name);
	[[nodiscard]] FRDGResource* Import(const char* Name) const;

	// ── Pass declaration ──

	FRDGPass& AddRasterPass(const char* Name, int32_t Layer = 0);
	FRDGPass& AddComputePass(const char* Name, int32_t Layer = 0);
	FRDGPass& AddCopyPass(const char* Name, int32_t Layer = 0);

	// ── Resource access declaration ──

	void Read(FRDGPass& Pass, FRDGResource* Resource, ERHIResourceState State);
	void Write(FRDGPass& Pass, FRDGResource* Resource, ERHIResourceState State);

	// ── Compilation & execution ──

	void Compile();
	void Execute();

	// ── Queries ──

	[[nodiscard]] FRDGResource* GetResource(const char* Name) const;
	[[nodiscard]] std::size_t GetPassCount() const { return Passes.size(); }

private:
	IRHI* RHI = nullptr;

	// Owns all RDG virtual resources
	std::vector<std::unique_ptr<FRDGResource>> OwnedResources;

	// Passes in declaration order (Compile reorders them)
	std::vector<std::unique_ptr<FRDGPass>> OwnedPasses;
	std::vector<FRDGPass*> Passes;

	// Name → resource lookup
	std::unordered_map<std::string, FRDGResource*> NamedResources;

	// Exported resources (cross-feature)
	std::unordered_map<std::string, FRDGResource*> ExportedResources;

	FRDGTransientPool TransientPool;

	// Compiled output
	struct FCompiledPass
	{
		FRDGPass* Pass;
		std::vector<std::pair<FRDGResource*, ERHIResourceState>> PreBarriers;
	};
	std::vector<FCompiledPass> CompiledPasses;

	// ── Compile helpers ──

	void CollectResourceLifetimes();
	void AllocateTransientResources();
	void SortPasses();
	void DeriveBarriers();

	struct FResourceLifetime
	{
		FRDGResource* Resource = nullptr;
		std::uint32_t FirstUse = 0;
		std::uint32_t LastUse = 0;
	};
	std::unordered_map<FRDGResource*, FResourceLifetime> Lifetimes;
};

} // namespace Maho
