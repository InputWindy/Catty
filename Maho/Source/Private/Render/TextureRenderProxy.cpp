#include "TextureRenderProxy.h"

#include <Core/System/Log.h>
#include <Render/RHI/RHI.h>
#include <Render/RHI/RHIResourceManager.h>
#include <Render/RHI/RHIServer.h>

#include <utility>

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
	bReady = false;
}

void FTextureProxyRegistry::BeginUploadOnRHI(
	IRHI& RHI,
	FTextureRenderProxy& Proxy,
	FTextureCpuSnapshot& Snapshot,
	FInFlightUpload& OutFlight)
{
	const FTransferHandle KeptHandle = OutFlight.Handle;
	OutFlight = FInFlightUpload{};
	OutFlight.Handle = KeptHandle;
	OutFlight.CatalogKey = Snapshot.CatalogKey;
	OutFlight.Generation = Snapshot.Generation;
	OutFlight.Proxy = &Proxy;

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
		Proxy.bReady = false;
	}

	if (Proxy.Texture && Snapshot.Generation <= Proxy.UploadedGeneration && Proxy.bReady)
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
		Proxy.bReady = false;
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

	OutFlight.Fence = Fence;
	OutFlight.Staging = Staging;
	OutFlight.CmdList = CmdList;
}

void FTextureProxyRegistry::CompleteUploadOnRHI(IRHI& RHI, FInFlightUpload& Flight, bool bSuccess)
{
	FRHIResourceManager& Manager = RHI.GetResourceManager();
	if (Flight.CmdList)
	{
		RHI.DestroyCommandList(Flight.CmdList);
		Flight.CmdList = nullptr;
	}
	if (Flight.Staging)
	{
		Manager.Release(Flight.Staging, true);
		Flight.Staging = nullptr;
	}
	if (Flight.Fence)
	{
		RHI.DestroyFence(Flight.Fence);
		Flight.Fence = nullptr;
	}

	if (bSuccess && Flight.Proxy)
	{
		Flight.Proxy->UploadedGeneration = Flight.Generation;
		Flight.Proxy->bReady = Flight.Proxy->Texture != nullptr;
		SetTransferHandleState(Flight.Handle, ETransferState::Succeeded);
	}
	else
	{
		SetTransferHandleState(Flight.Handle, ETransferState::Failed);
	}
}

void FTextureProxyRegistry::BeginUpload(
	FRHIServer& RHIServer,
	FTextureCpuSnapshot&& Snapshot,
	FTransferHandle Handle)
{
	if (!RHIServer.HasRHI() || !RHIServer.IsInitialized())
	{
		MAHO_CORE_ERROR("TextureProxy: RHIServer unavailable for '{}'", Snapshot.CatalogKey);
		SetTransferHandleState(Handle, ETransferState::Failed);
		return;
	}
	if (!IsSupportedUpload(Snapshot))
	{
		MAHO_CORE_WARN("TextureProxy: skip unsupported '{}'", Snapshot.CatalogKey);
		SetTransferHandleState(Handle, ETransferState::Failed);
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
	Proxy->bReady = false;

	FRHIServer* ServerPtr = &RHIServer;
	RHIServer.Enqueue(
		[this, ServerPtr, Proxy, Snap = std::move(Snapshot), Handle](FThreadedServer& /*Server*/) mutable
		{
			IRHI* RHI = ServerPtr->GetRHI();
			if (!RHI)
			{
				SetTransferHandleState(Handle, ETransferState::Failed);
				return;
			}

			FInFlightUpload Flight{};
			Flight.Handle = Handle;
			BeginUploadOnRHI(*RHI, *Proxy, Snap, Flight);
			if (!Flight.Fence)
			{
				if (Proxy->bReady && Snap.Generation <= Proxy->UploadedGeneration)
				{
					SetTransferHandleState(Handle, ETransferState::Succeeded);
				}
				else
				{
					CompleteUploadOnRHI(*RHI, Flight, false);
				}
				return;
			}
			InFlight.push_back(std::move(Flight));
		});
}

void FTextureProxyRegistry::PollInFlight(FRHIServer& RHIServer)
{
	if (!RHIServer.HasRHI() || !RHIServer.IsInitialized() || InFlight.empty())
	{
		return;
	}

	FRHIServer* ServerPtr = &RHIServer;
	RHIServer.Enqueue(
		[this, ServerPtr](FThreadedServer& /*Server*/)
		{
			IRHI* RHI = ServerPtr->GetRHI();
			if (!RHI)
			{
				return;
			}

			std::vector<FInFlightUpload> StillInFlight;
			StillInFlight.reserve(InFlight.size());
			for (FInFlightUpload& Flight : InFlight)
			{
				if (!Flight.Fence || !RHI->IsFenceSignaled(Flight.Fence))
				{
					StillInFlight.push_back(std::move(Flight));
					continue;
				}
				CompleteUploadOnRHI(*RHI, Flight, true);
			}
			InFlight = std::move(StillInFlight);
		});
}

void FTextureProxyRegistry::Destroy(
	FRHIServer& RHIServer,
	const std::string& CatalogKey,
	FTransferHandle Handle)
{
	if (CatalogKey == DefaultKey)
	{
		SetTransferHandleState(Handle, ETransferState::Succeeded);
		return;
	}

	const auto It = Proxies.find(CatalogKey);
	if (It == Proxies.end() || !It->second)
	{
		SetTransferHandleState(Handle, ETransferState::Succeeded);
		return;
	}

	std::unique_ptr<FTextureRenderProxy> Owned = std::move(It->second);
	Proxies.erase(It);

	if (!RHIServer.HasRHI() || !RHIServer.IsInitialized())
	{
		SetTransferHandleState(Handle, ETransferState::Succeeded);
		return;
	}

	FRHIServer* ServerPtr = &RHIServer;
	FTextureRenderProxy* Proxy = Owned.release();
	RHIServer.Enqueue(
		[ServerPtr, Proxy, Handle](FThreadedServer& /*Server*/)
		{
			if (IRHI* RHI = ServerPtr->GetRHI())
			{
				Proxy->Release(RHI->GetResourceManager());
			}
			delete Proxy;
			SetTransferHandleState(Handle, ETransferState::Succeeded);
		});
}

void FTextureProxyRegistry::DestroyAll(FRHIServer& RHIServer)
{
	std::vector<std::string> Keys;
	Keys.reserve(Proxies.size());
	for (const auto& Pair : Proxies)
	{
		if (Pair.first != DefaultKey)
		{
			Keys.push_back(Pair.first);
		}
	}
	for (const std::string& Key : Keys)
	{
		Destroy(RHIServer, Key, AllocateTransferHandle(ETransferState::InProgress));
	}

	if (RHIServer.HasRHI() && RHIServer.IsInitialized())
	{
		RHIServer.Flush();
	}

	const auto DefaultIt = Proxies.find(DefaultKey);
	if (DefaultIt != Proxies.end() && DefaultIt->second && RHIServer.HasRHI() && RHIServer.IsInitialized())
	{
		FRHIServer* ServerPtr = &RHIServer;
		FTextureRenderProxy* Proxy = DefaultIt->second.get();
		RHIServer.Enqueue(
			[ServerPtr, Proxy](FThreadedServer& /*Server*/)
			{
				if (IRHI* RHI = ServerPtr->GetRHI())
				{
					Proxy->Release(RHI->GetResourceManager());
				}
			});
		RHIServer.Flush();
		Proxies.erase(DefaultIt);
	}
	else
	{
		Proxies.clear();
	}
	InFlight.clear();
}

void FTextureProxyRegistry::EnsureDefaultPlaceholder(FRHIServer& RHIServer)
{
	if (FindProxy(DefaultKey))
	{
		return;
	}

	FTextureCpuSnapshot Snap{};
	Snap.CatalogKey = DefaultKey;
	Snap.Generation = 1;
	Snap.Dimension = ETextureDimension::Tex2D;
	Snap.PixelFormat = ETexturePixelFormat::RGBA8;
	Snap.Width = 1;
	Snap.Height = 1;
	Snap.bSRGB = true;
	Snap.Pixels = { 255, 0, 255, 255 };

	FTransferHandle Handle = AllocateTransferHandle(ETransferState::InProgress);
	BeginUpload(RHIServer, std::move(Snap), Handle);
	RHIServer.Flush();
	PollInFlight(RHIServer);
	RHIServer.Flush();
	if (IRHI* RHI = RHIServer.GetRHI())
	{
		(void)RHI;
	}
	// Drain until ready or failed (Boot only).
	for (int Attempt = 0; Attempt < 64; ++Attempt)
	{
		if (FTextureRenderProxy* Proxy = FindProxy(DefaultKey); Proxy && Proxy->IsReady())
		{
			return;
		}
		PollInFlight(RHIServer);
		RHIServer.Flush();
		if (Handle.HasFailed())
		{
			MAHO_CORE_ERROR("TextureProxy: default placeholder upload failed");
			return;
		}
		if (Handle.HasSucceeded())
		{
			return;
		}
	}
	MAHO_CORE_WARN("TextureProxy: default placeholder still pending after Boot polls");
}

FRHITexture* FTextureProxyRegistry::FindTexture(const std::string& CatalogKey) const
{
	if (const FTextureRenderProxy* Proxy = FindProxy(CatalogKey))
	{
		return Proxy->IsReady() ? Proxy->GetRHITexture() : nullptr;
	}
	return nullptr;
}

FRHITexture* FTextureProxyRegistry::FindTextureOrDefault(const std::string& CatalogKey) const
{
	if (FRHITexture* Texture = FindTexture(CatalogKey))
	{
		return Texture;
	}
	if (const FTextureRenderProxy* DefaultProxy = GetDefaultProxy())
	{
		return DefaultProxy->GetRHITexture();
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

FTextureRenderProxy* FTextureProxyRegistry::GetDefaultProxy() const
{
	return FindProxy(DefaultKey);
}

} // namespace Maho
