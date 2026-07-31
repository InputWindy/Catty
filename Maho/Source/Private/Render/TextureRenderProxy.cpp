#include "TextureRenderProxy.h"

#include <Core/Object/SoftObjectPath.h>
#include <Core/System/Log.h>
#include <Render/RHI/RHI.h>
#include <Render/RHI/RHIResourceManager.h>
#include <Render/RHI/RHIServer.h>

namespace Maho
{

namespace
{

[[nodiscard]] bool IsSupportedUpload(const FTextureCpuSnapshot& Snapshot)
{
	if (Snapshot.CatalogKey.empty() || Snapshot.Pixels.empty())
	{
		return false;
	}
	if (Snapshot.Dimension != ETextureDimension::Tex2D)
	{
		return false;
	}
	if (Snapshot.PixelFormat != ETexturePixelFormat::RGBA8)
	{
		return false;
	}
	if (Snapshot.Width == 0 || Snapshot.Height == 0)
	{
		return false;
	}
	const std::size_t Expected =
		static_cast<std::size_t>(Snapshot.Width) * static_cast<std::size_t>(Snapshot.Height) * 4u;
	return Snapshot.Pixels.size() >= Expected;
}

[[nodiscard]] bool DescEquals(const FRHITextureDesc& A, const FRHITextureDesc& B)
{
	return A.Format == B.Format
		&& A.Dimension == B.Dimension
		&& A.Extent.Width == B.Extent.Width
		&& A.Extent.Height == B.Extent.Height
		&& A.Extent.Depth == B.Extent.Depth
		&& A.MipLevels == B.MipLevels
		&& A.ArrayLayers == B.ArrayLayers;
}

} // namespace

void FTextureProxyRegistry::UploadSnapshotOnRHI(IRHI& RHI, FTextureRenderProxy& Proxy, FTextureCpuSnapshot& Snapshot)
{
	FRHITextureDesc Desc{};
	if (!TryBuildTextureDesc(Snapshot, Desc))
	{
		MAHO_CORE_ERROR("TextureProxy: unsupported snapshot '{}'", Snapshot.CatalogKey);
		return;
	}

	FRHIResourceManager& Manager = RHI.GetResourceManager();

	if (Proxy.Texture && !DescEquals(Proxy.CachedDesc, Desc))
	{
		Manager.Release(Proxy.Texture, true);
		Proxy.Texture = nullptr;
	}

	if (Proxy.Texture && Snapshot.Generation <= Proxy.UploadedGeneration)
	{
		return;
	}

	if (!Proxy.Texture)
	{
		Proxy.Texture = Manager.AcquireTexture(Desc, Snapshot.CatalogKey.c_str());
		if (!Proxy.Texture)
		{
			MAHO_CORE_ERROR("TextureProxy: AcquireTexture failed '{}'", Snapshot.CatalogKey);
			return;
		}
		Proxy.CachedDesc = Desc;
	}

	FRHIBufferDesc StagingDesc{};
	StagingDesc.Size = Snapshot.Pixels.size();
	StagingDesc.Usage = ERHIBufferUsage::TransferSrc;
	StagingDesc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;

	FRHIBuffer* Staging = Manager.AcquireBuffer(StagingDesc);
	if (!Staging)
	{
		MAHO_CORE_ERROR("TextureProxy: AcquireBuffer staging failed '{}'", Snapshot.CatalogKey);
		return;
	}

	RHI.UpdateBuffer(Staging, 0, StagingDesc.Size, Snapshot.Pixels.data());

	FRHICommandList* CmdList = RHI.CreateCommandList(ERHICommandListType::Transfer);
	if (!CmdList)
	{
		Manager.Release(Staging, true);
		MAHO_CORE_ERROR("TextureProxy: CreateCommandList failed '{}'", Snapshot.CatalogKey);
		return;
	}

	CmdList->Begin();
	CmdList->TransitionTexture(Proxy.Texture, ERHIResourceState::Common, ERHIResourceState::CopyDst);
	CmdList->CopyBufferToTexture(Staging, Proxy.Texture, 0);
	CmdList->TransitionTexture(Proxy.Texture, ERHIResourceState::CopyDst, ERHIResourceState::ShaderResource);
	CmdList->End();

	FRHIFence* Fence = RHI.CreateFence(false);
	FRHICommandList* Lists[] = { CmdList };
	RHI.GetTransferQueue().Submit(Lists, 1, nullptr, 0, nullptr, 0, Fence);
	if (Fence)
	{
		RHI.WaitForFence(Fence);
		RHI.DestroyFence(Fence);
	}
	RHI.DestroyCommandList(CmdList);
	Manager.Release(Staging, true);

	Proxy.UploadedGeneration = Snapshot.Generation;
}

bool TryBuildTextureCpuSnapshot(const UTexture& Texture, FTextureCpuSnapshot& Out)
{
	if (Texture.GetLoadState() != EResourceLoadState::Ready || Texture.GetPixels().empty())
	{
		return false;
	}

	Out = FTextureCpuSnapshot{};
	Out.CatalogKey = FSoftObjectPath::FromObject(Texture).GetAssetPathString();
	if (Out.CatalogKey.empty())
	{
		Out.CatalogKey = Texture.GetPathName();
	}
	Out.Generation = Texture.GetContentGeneration();
	Out.Dimension = Texture.GetDimension();
	Out.PixelFormat = Texture.GetPixelFormat();
	Out.Width = Texture.GetWidth();
	Out.Height = Texture.GetHeight();
	Out.Depth = Texture.GetDepth();
	Out.ArrayLayers = Texture.GetArrayLayers();
	Out.MipCount = Texture.GetMipCount();
	Out.bSRGB = Texture.IsSRGB();
	Out.Pixels = Texture.GetPixels();
	return IsSupportedUpload(Out);
}

ERHIFormat MapTexturePixelFormatToRHI(ETexturePixelFormat Format, bool bSRGB)
{
	(void)bSRGB;
	switch (Format)
	{
	case ETexturePixelFormat::RGBA8:
		return ERHIFormat::R8G8B8A8_UNORM;
	default:
		return ERHIFormat::Unknown;
	}
}

ERHITextureDimension MapTextureDimensionToRHI(ETextureDimension Dimension)
{
	switch (Dimension)
	{
	case ETextureDimension::Tex2D:
		return ERHITextureDimension::Tex2D;
	case ETextureDimension::Tex2DArray:
		return ERHITextureDimension::Tex2DArray;
	case ETextureDimension::Cube:
		return ERHITextureDimension::Cube;
	case ETextureDimension::Tex3D:
		return ERHITextureDimension::Tex3D;
	default:
		return ERHITextureDimension::Tex2D;
	}
}

bool TryBuildTextureDesc(const FTextureCpuSnapshot& Snapshot, FRHITextureDesc& OutDesc)
{
	if (!IsSupportedUpload(Snapshot))
	{
		return false;
	}

	OutDesc = FRHITextureDesc{};
	OutDesc.Format = MapTexturePixelFormatToRHI(Snapshot.PixelFormat, Snapshot.bSRGB);
	if (OutDesc.Format == ERHIFormat::Unknown)
	{
		return false;
	}
	OutDesc.Dimension = MapTextureDimensionToRHI(Snapshot.Dimension);
	OutDesc.Extent.Width = Snapshot.Width;
	OutDesc.Extent.Height = Snapshot.Height;
	OutDesc.Extent.Depth = Snapshot.Depth;
	OutDesc.MipLevels = Snapshot.MipCount;
	OutDesc.ArrayLayers = Snapshot.ArrayLayers;
	OutDesc.Usage = ERHITextureUsage::Sampled | ERHITextureUsage::TransferDst;
	OutDesc.MemoryUsage = ERHIMemoryUsage::GPUOnly;
	return true;
}

FTextureRenderProxy::FTextureRenderProxy(std::string InCatalogKey)
	: CatalogKey(std::move(InCatalogKey))
{
}

void FTextureRenderProxy::Release(FRHIResourceManager& Manager)
{
	if (Texture)
	{
		Manager.Release(Texture, true);
		Texture = nullptr;
	}
	UploadedGeneration = 0;
	CachedDesc = FRHITextureDesc{};
}

void FTextureProxyRegistry::UploadOrUpdate(FRHIServer& RHIServer, FTextureCpuSnapshot&& Snapshot)
{
	if (!RHIServer.HasRHI() || !RHIServer.IsInitialized())
	{
		MAHO_CORE_ERROR("TextureProxy: RHIServer unavailable for '{}'", Snapshot.CatalogKey);
		return;
	}
	if (!IsSupportedUpload(Snapshot))
	{
		MAHO_CORE_WARN("TextureProxy: skip unsupported '{}'", Snapshot.CatalogKey);
		return;
	}

	const std::string Key = Snapshot.CatalogKey;
	FTextureRenderProxy* Proxy = FindProxy(Key);
	if (!Proxy)
	{
		auto NewProxy = std::make_unique<FTextureRenderProxy>(Key);
		Proxy = NewProxy.get();
		Proxies.emplace(Key, std::move(NewProxy));
	}

	FRHIServer* ServerPtr = &RHIServer;
	RHIServer.Enqueue(
		[ServerPtr, Proxy, Snap = std::move(Snapshot)](FThreadedServer& /*Server*/) mutable
		{
			IRHI* RHI = ServerPtr->GetRHI();
			if (!RHI)
			{
				return;
			}
			FTextureProxyRegistry::UploadSnapshotOnRHI(*RHI, *Proxy, Snap);
		});
	RHIServer.Flush();
}

void FTextureProxyRegistry::Destroy(FRHIServer& RHIServer, const std::string& CatalogKey)
{
	const auto It = Proxies.find(CatalogKey);
	if (It == Proxies.end() || !It->second)
	{
		return;
	}

	FTextureRenderProxy* Proxy = It->second.get();
	if (RHIServer.HasRHI() && RHIServer.IsInitialized() && Proxy->Texture)
	{
		FRHIServer* ServerPtr = &RHIServer;
		RHIServer.Enqueue(
			[ServerPtr, Proxy](FThreadedServer& /*Server*/)
			{
				if (IRHI* RHI = ServerPtr->GetRHI())
				{
					Proxy->Release(RHI->GetResourceManager());
				}
			});
		RHIServer.Flush();
	}
	Proxies.erase(It);
}

void FTextureProxyRegistry::DestroyAll(FRHIServer& RHIServer)
{
	std::vector<std::string> Keys;
	Keys.reserve(Proxies.size());
	for (const auto& Pair : Proxies)
	{
		Keys.push_back(Pair.first);
	}
	for (const std::string& Key : Keys)
	{
		Destroy(RHIServer, Key);
	}
}

FRHITexture* FTextureProxyRegistry::FindTexture(const std::string& CatalogKey) const
{
	if (const FTextureRenderProxy* Proxy = FindProxy(CatalogKey))
	{
		return Proxy->GetRHITexture();
	}
	return nullptr;
}

FTextureRenderProxy* FTextureProxyRegistry::FindProxy(const std::string& CatalogKey) const
{
	const auto It = Proxies.find(CatalogKey);
	if (It == Proxies.end())
	{
		return nullptr;
	}
	return It->second.get();
}

} // namespace Maho
