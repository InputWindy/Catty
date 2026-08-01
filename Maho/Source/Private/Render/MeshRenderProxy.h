#pragma once

#include <Core/Server/TransferHandle.h>
#include <Render/RenderFramePacket.h>
#include <Render/RHI/RHIResources.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Maho
{

class FRHIServer;
class FRHIResourceManager;
class IRHI;
class FRHIFence;
class FRHICommandList;
class FRHIBuffer;

[[nodiscard]] bool TryBuildMeshCpuSnapshot(const UStaticMesh& Mesh, FMeshCpuSnapshot& Out);

class FMeshRenderProxy
{
public:
	explicit FMeshRenderProxy(std::string InCatalogKey);

	[[nodiscard]] const std::string& GetCatalogKey() const { return CatalogKey; }
	[[nodiscard]] std::uint64_t GetUploadedGeneration() const { return UploadedGeneration; }
	[[nodiscard]] FRHIBuffer* GetVertexBuffer() const { return VertexBuffer; }
	[[nodiscard]] FRHIBuffer* GetIndexBuffer() const { return IndexBuffer; }
	[[nodiscard]] std::uint32_t GetIndexCount() const { return IndexCount; }
	[[nodiscard]] std::uint32_t GetVertexStride() const { return VertexStride; }
	[[nodiscard]] bool HasSkinning() const { return bHasSkinning; }
	[[nodiscard]] bool IsReady() const { return bReady && VertexBuffer && IndexBuffer && IndexCount > 0; }

	void Release(FRHIResourceManager& Manager);

private:
	friend class FMeshProxyRegistry;

	std::string CatalogKey;
	FRHIBuffer* VertexBuffer = nullptr;
	FRHIBuffer* IndexBuffer = nullptr;
	std::uint32_t IndexCount = 0;
	std::uint32_t VertexStride = 0;
	std::uint64_t UploadedGeneration = 0;
	bool bHasSkinning = false;
	bool bReady = false;
};

class FMeshProxyRegistry
{
public:
	static constexpr const char* DefaultCatalogKey = "__Maho.Default.StaticMesh";

	void BeginUpload(FRHIServer& RHIServer, FMeshCpuSnapshot&& Snapshot, FTransferHandle Handle);
	void PollInFlight(FRHIServer& RHIServer);
	void Destroy(FRHIServer& RHIServer, const std::string& CatalogKey, FTransferHandle Handle);
	void DestroyAll(FRHIServer& RHIServer);
	void EnsureDefaultPlaceholder(FRHIServer& RHIServer);

	[[nodiscard]] FMeshRenderProxy* FindProxy(const std::string& CatalogKey) const;
	[[nodiscard]] FMeshRenderProxy* FindProxyOrDefault(const std::string& CatalogKey) const;
	[[nodiscard]] FMeshRenderProxy* GetDefaultProxy() const;

private:
	struct FInFlightUpload
	{
		FTransferHandle Handle;
		std::string CatalogKey;
		std::uint64_t Generation = 0;
		FMeshRenderProxy* Proxy = nullptr;
		FRHIFence* Fence = nullptr;
		FRHIBuffer* StagingVB = nullptr;
		FRHIBuffer* StagingIB = nullptr;
		FRHICommandList* CmdList = nullptr;
		std::uint32_t IndexCount = 0;
		std::uint32_t VertexStride = 0;
		bool bHasSkinning = false;
	};

	static void BeginUploadOnRHI(
		IRHI& RHI,
		FMeshRenderProxy& Proxy,
		FMeshCpuSnapshot& Snapshot,
		FInFlightUpload& OutFlight);
	static void CompleteUploadOnRHI(IRHI& RHI, FInFlightUpload& Flight, bool bSuccess);

	std::unordered_map<std::string, std::unique_ptr<FMeshRenderProxy>> Proxies;
	std::vector<FInFlightUpload> InFlight;
	std::string DefaultKey = DefaultCatalogKey;
};

} // namespace Maho
