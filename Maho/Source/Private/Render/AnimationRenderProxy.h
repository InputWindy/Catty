#pragma once

#include <Core/Server/TransferHandle.h>
#include <Render/RenderFramePacket.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Maho
{

class FRHIServer;

[[nodiscard]] bool TryBuildAnimationCpuSnapshot(const UAnimation& Animation, FAnimationCpuSnapshot& Out);

class FAnimationRenderProxy
{
public:
	explicit FAnimationRenderProxy(std::string InCatalogKey);

	[[nodiscard]] const std::string& GetCatalogKey() const { return CatalogKey; }
	[[nodiscard]] std::uint64_t GetUploadedGeneration() const { return UploadedGeneration; }
	[[nodiscard]] const std::string& GetSkeletonCatalogKey() const { return SkeletonCatalogKey; }
	[[nodiscard]] float GetDurationSeconds() const { return DurationSeconds; }
	[[nodiscard]] const std::vector<FAnimationTrackSnapshot>& GetTracks() const { return Tracks; }
	[[nodiscard]] bool IsReady() const { return bReady; }

private:
	friend class FAnimationProxyRegistry;

	std::string CatalogKey;
	std::string SkeletonCatalogKey;
	float DurationSeconds = 0.f;
	std::vector<FAnimationTrackSnapshot> Tracks;
	std::uint64_t UploadedGeneration = 0;
	bool bReady = false;
};

class FAnimationProxyRegistry
{
public:
	static constexpr const char* DefaultCatalogKey = "__Maho.Default.Animation";

	/** CPU-only apply on MahoRender (no RHI). Completes the transfer handle immediately. */
	void BeginUpload(FRHIServer& RHIServer, FAnimationCpuSnapshot&& Snapshot, FTransferHandle Handle);
	void Destroy(FRHIServer& RHIServer, const std::string& CatalogKey, FTransferHandle Handle);
	void DestroyAll(FRHIServer& RHIServer);
	void EnsureDefaultPlaceholder();

	[[nodiscard]] FAnimationRenderProxy* FindProxy(const std::string& CatalogKey) const;
	[[nodiscard]] FAnimationRenderProxy* FindProxyOrDefault(const std::string& CatalogKey) const;
	[[nodiscard]] FAnimationRenderProxy* GetDefaultProxy() const;

private:
	std::unordered_map<std::string, std::unique_ptr<FAnimationRenderProxy>> Proxies;
	std::string DefaultKey = DefaultCatalogKey;
};

} // namespace Maho
