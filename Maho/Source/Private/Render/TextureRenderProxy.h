#pragma once

#include <Render/RenderFramePacket.h>

#include <Render/RHI/RHIResources.h>
#include <Render/RHI/RHIEnums.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace Maho
{

class FRHIServer;
class FRHIResourceManager;
class IRHI;

[[nodiscard]] bool TryBuildTextureCpuSnapshot(const UTexture& Texture, FTextureCpuSnapshot& Out);

[[nodiscard]] ERHIFormat MapTexturePixelFormatToRHI(ETexturePixelFormat Format, bool bSRGB);
[[nodiscard]] ERHITextureDimension MapTextureDimensionToRHI(ETextureDimension Dimension);
[[nodiscard]] bool TryBuildTextureDesc(const FTextureCpuSnapshot& Snapshot, FRHITextureDesc& OutDesc);

class FTextureRenderProxy
{
public:
	explicit FTextureRenderProxy(std::string InCatalogKey);

	[[nodiscard]] const std::string& GetCatalogKey() const { return CatalogKey; }
	[[nodiscard]] std::uint64_t GetUploadedGeneration() const { return UploadedGeneration; }
	[[nodiscard]] FRHITexture* GetRHITexture() const { return Texture; }

	void Release(FRHIResourceManager& Manager);

private:
	friend class FTextureProxyRegistry;

	std::string CatalogKey;
	std::uint64_t UploadedGeneration = 0;
	FRHITexture* Texture = nullptr;
	FRHITextureDesc CachedDesc{};
};

class FTextureProxyRegistry
{
public:
	/** Runs Acquire + staging upload on MahoRHI (Enqueue+Flush). Call from MahoRender. */
	void UploadOrUpdate(FRHIServer& RHIServer, FTextureCpuSnapshot&& Snapshot);
	void Destroy(FRHIServer& RHIServer, const std::string& CatalogKey);
	void DestroyAll(FRHIServer& RHIServer);

	[[nodiscard]] FRHITexture* FindTexture(const std::string& CatalogKey) const;
	[[nodiscard]] FTextureRenderProxy* FindProxy(const std::string& CatalogKey) const;

private:
	static void UploadSnapshotOnRHI(IRHI& RHI, FTextureRenderProxy& Proxy, FTextureCpuSnapshot& Snapshot);

	std::unordered_map<std::string, std::unique_ptr<FTextureRenderProxy>> Proxies;
};

} // namespace Maho
