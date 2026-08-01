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

[[nodiscard]] bool TryBuildSkeletonCpuSnapshot(const USkeleton& Skeleton, FSkeletonCpuSnapshot& Out);

class FSkeletonRenderProxy
{
public:
	explicit FSkeletonRenderProxy(std::string InCatalogKey);

	[[nodiscard]] const std::string& GetCatalogKey() const { return CatalogKey; }
	[[nodiscard]] std::uint64_t GetUploadedGeneration() const { return UploadedGeneration; }
	[[nodiscard]] std::uint32_t GetBoneCount() const { return BoneCount; }
	[[nodiscard]] const std::vector<std::string>& GetBoneNames() const { return BoneNames; }
	[[nodiscard]] const std::vector<std::int32_t>& GetParentIndex() const { return ParentIndex; }
	[[nodiscard]] const std::vector<float>& GetInverseBindPose() const { return InverseBindPose; }
	[[nodiscard]] FRHIBuffer* GetIbmBuffer() const { return IbmBuffer; }
	[[nodiscard]] bool IsReady() const { return bReady && BoneCount > 0; }

	void Release(FRHIResourceManager& Manager);

private:
	friend class FSkeletonProxyRegistry;

	std::string CatalogKey;
	std::uint32_t BoneCount = 0;
	std::vector<std::string> BoneNames;
	std::vector<std::int32_t> ParentIndex;
	std::vector<float> InverseBindPose;
	FRHIBuffer* IbmBuffer = nullptr;
	std::uint64_t UploadedGeneration = 0;
	bool bReady = false;
};

class FSkeletonProxyRegistry
{
public:
	static constexpr const char* DefaultCatalogKey = "__Maho.Default.Skeleton";

	/** Apply CPU snapshot; optional IBM buffer upload is async when RHI is up. */
	void BeginUpload(FRHIServer& RHIServer, FSkeletonCpuSnapshot&& Snapshot, FTransferHandle Handle);
	void PollInFlight(FRHIServer& RHIServer);
	void Destroy(FRHIServer& RHIServer, const std::string& CatalogKey, FTransferHandle Handle);
	void DestroyAll(FRHIServer& RHIServer);
	void EnsureDefaultPlaceholder(FRHIServer& RHIServer);

	[[nodiscard]] FSkeletonRenderProxy* FindProxy(const std::string& CatalogKey) const;
	[[nodiscard]] FSkeletonRenderProxy* FindProxyOrDefault(const std::string& CatalogKey) const;
	[[nodiscard]] FSkeletonRenderProxy* GetDefaultProxy() const;

private:
	struct FInFlightUpload
	{
		FTransferHandle Handle;
		std::string CatalogKey;
		std::uint64_t Generation = 0;
		FSkeletonRenderProxy* Proxy = nullptr;
		FRHIFence* Fence = nullptr;
		FRHIBuffer* Staging = nullptr;
		FRHICommandList* CmdList = nullptr;
		bool bGpuPending = false;
	};

	std::unordered_map<std::string, std::unique_ptr<FSkeletonRenderProxy>> Proxies;
	std::vector<FInFlightUpload> InFlight;
	std::string DefaultKey = DefaultCatalogKey;
};

} // namespace Maho
