#pragma once

#include <Core/Export.h>
#include <Render/RHI/RHICommandList.h>
#include <Render/RHI/RHIEnums.h>
#include <Render/RHI/RHIResources.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace Maho
{

class FRDGResource;

enum class ERDGPassType : uint8_t
{
	Raster,
	Compute,
	Copy
};

struct FRDGResourceAccess
{
	FRDGResource* Resource = nullptr;
	ERHIResourceState RequiredState = ERHIResourceState::Common;
};

class MAHO_API FRDGPass
{
public:
	using FExecuteFunc = std::function<void(FRHICommandList&)>;

	FRDGPass(const char* Name, ERDGPassType Type);
	FRDGPass(const FRDGPass&) = delete;
	FRDGPass& operator=(const FRDGPass&) = delete;
	~FRDGPass() = default;

	[[nodiscard]] const char* GetName() const { return Name; }
	[[nodiscard]] ERDGPassType GetType() const { return Type; }
	[[nodiscard]] int32_t GetLayer() const { return Layer; }

	void AddRead(FRDGResource* Resource, ERHIResourceState State);
	void AddWrite(FRDGResource* Resource, ERHIResourceState State);
	[[nodiscard]] const std::vector<FRDGResourceAccess>& GetReads() const { return Reads; }
	[[nodiscard]] const std::vector<FRDGResourceAccess>& GetWrites() const { return Writes; }

	void SetExecute(FExecuteFunc InExecute) { Execute = std::move(InExecute); }
	[[nodiscard]] FExecuteFunc& GetExecute() { return Execute; }

	// Raster pass only
	void SetRenderTargets(FRHIRenderPass* RP, FRHIFramebuffer* FB)
	{
		RenderPassHandle = RP;
		FramebufferHandle = FB;
	}
	[[nodiscard]] FRHIRenderPass* GetRenderPass() const { return RenderPassHandle; }
	[[nodiscard]] FRHIFramebuffer* GetFramebuffer() const { return FramebufferHandle; }

private:
	friend class FRDGBuilder;
	void SetLayer(int32_t InLayer) { Layer = InLayer; }

	const char* Name = nullptr;
	ERDGPassType Type = ERDGPassType::Raster;
	int32_t Layer = 0;
	std::vector<FRDGResourceAccess> Reads;
	std::vector<FRDGResourceAccess> Writes;
	FExecuteFunc Execute;
	FRHIRenderPass* RenderPassHandle = nullptr;
	FRHIFramebuffer* FramebufferHandle = nullptr;
};

} // namespace Maho
