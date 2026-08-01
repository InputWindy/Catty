#pragma once

#include <Core/Server/TransferHandle.h>
#include <Render/RenderFramePacket.h>
#include <Render/RHI/RHIEnums.h>
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
	[[nodiscard]] bool IsReady() const { return Texture != nullptr && bReady; }

	void Release(FRHIResourceManager& Manager);

private:
	friend class FTextureProxyRegistry;

	std::string CatalogKey;
	std::uint64_t UploadedGeneration = 0;
	FRHITexture* Texture = nullptr;
	FRHITextureDesc CachedDesc{};
	bool bReady = false;
};

class FTextureProxyRegistry
{
public:
	static constexpr const char* DefaultCatalogKey = "__Maho.Default.Texture2D";

	/** Begin async GPU upload on MahoRHI (no Flush / no WaitForFence on caller). */
	void BeginUpload(FRHIServer& RHIServer, FTextureCpuSnapshot&& Snapshot, FTransferHandle Handle);
	void PollInFlight(FRHIServer& RHIServer);
	void Destroy(FRHIServer& RHIServer, const std::string& CatalogKey, FTransferHandle Handle);
	void DestroyAll(FRHIServer& RHIServer);

	/** Boot: create 1x1 placeholder (may Flush RHI — Boot only). */
	void EnsureDefaultPlaceholder(FRHIServer& RHIServer);

	[[nodiscard]] FRHITexture* FindTexture(const std::string& CatalogKey) const;
	[[nodiscard]] FRHITexture* FindTextureOrDefault(const std::string& CatalogKey) const;
	[[nodiscard]] FTextureRenderProxy* FindProxy(const std::string& CatalogKey) const;
	[[nodiscard]] FTextureRenderProxy* GetDefaultProxy() const;

private:
	struct FInFlightUpload
	{
		FTransferHandle Handle;
		std::string CatalogKey;
		std::uint64_t Generation = 0;
		FTextureRenderProxy* Proxy = nullptr;
		FRHIFence* Fence = nullptr;
		FRHIBuffer* Staging = nullptr;
		FRHICommandList* CmdList = nullptr;
	};

	static void BeginUploadOnRHI(
		IRHI& RHI,
		FTextureRenderProxy& Proxy,
		FTextureCpuSnapshot& Snapshot,
		FInFlightUpload& OutFlight);
	static void CompleteUploadOnRHI(IRHI& RHI, FInFlightUpload& Flight, bool bSuccess);

	std::unordered_map<std::string, std::unique_ptr<FTextureRenderProxy>> Proxies;
	std::vector<FInFlightUpload> InFlight;
	std::string DefaultKey = DefaultCatalogKey;
};

} // namespace Maho
