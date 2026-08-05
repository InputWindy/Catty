#include "SkeletonRenderProxy.h"

#include <Core/System/Log.h>
#include <Render/RHI/RHI.h>
#include <Render/RHI/RHIResourceManager.h>
#include <Render/RHI/RHIServer.h>

#include <cmath>
#include <cstring>
#include <utility>

namespace Maho
{

namespace
{

void MulMat4(const float* A, const float* B, float* Out)
{
	float Tmp[16];
	for (int Row = 0; Row < 4; ++Row)
	{
		for (int Col = 0; Col < 4; ++Col)
		{
			Tmp[Row * 4 + Col] =
				A[Row * 4 + 0] * B[0 * 4 + Col]
				+ A[Row * 4 + 1] * B[1 * 4 + Col]
				+ A[Row * 4 + 2] * B[2 * 4 + Col]
				+ A[Row * 4 + 3] * B[3 * 4 + Col];
		}
	}
	std::memcpy(Out, Tmp, sizeof(Tmp));
}

[[nodiscard]] bool InvertMat4(const float* M, float* Out)
{
	float Inv[16];
	Inv[0] = M[5] * M[10] * M[15] - M[5] * M[11] * M[14] - M[9] * M[6] * M[15]
		+ M[9] * M[7] * M[14] + M[13] * M[6] * M[11] - M[13] * M[7] * M[10];
	Inv[4] = -M[4] * M[10] * M[15] + M[4] * M[11] * M[14] + M[8] * M[6] * M[15]
		- M[8] * M[7] * M[14] - M[12] * M[6] * M[11] + M[12] * M[7] * M[10];
	Inv[8] = M[4] * M[9] * M[15] - M[4] * M[11] * M[13] - M[8] * M[5] * M[15]
		+ M[8] * M[7] * M[13] + M[12] * M[5] * M[11] - M[12] * M[7] * M[9];
	Inv[12] = -M[4] * M[9] * M[14] + M[4] * M[10] * M[13] + M[8] * M[5] * M[14]
		- M[8] * M[6] * M[13] - M[12] * M[5] * M[10] + M[12] * M[6] * M[9];
	Inv[1] = -M[1] * M[10] * M[15] + M[1] * M[11] * M[14] + M[9] * M[2] * M[15]
		- M[9] * M[3] * M[14] - M[13] * M[2] * M[11] + M[13] * M[3] * M[10];
	Inv[5] = M[0] * M[10] * M[15] - M[0] * M[11] * M[14] - M[8] * M[2] * M[15]
		+ M[8] * M[3] * M[14] + M[12] * M[2] * M[11] - M[12] * M[3] * M[10];
	Inv[9] = -M[0] * M[9] * M[15] + M[0] * M[11] * M[13] + M[8] * M[1] * M[15]
		- M[8] * M[3] * M[13] - M[12] * M[1] * M[11] + M[12] * M[3] * M[9];
	Inv[13] = M[0] * M[9] * M[14] - M[0] * M[10] * M[13] - M[8] * M[1] * M[14]
		+ M[8] * M[2] * M[13] + M[12] * M[1] * M[10] - M[12] * M[2] * M[9];
	Inv[2] = M[1] * M[6] * M[15] - M[1] * M[7] * M[14] - M[5] * M[2] * M[15]
		+ M[5] * M[3] * M[14] + M[13] * M[2] * M[7] - M[13] * M[3] * M[6];
	Inv[6] = -M[0] * M[6] * M[15] + M[0] * M[7] * M[14] + M[4] * M[2] * M[15]
		- M[4] * M[3] * M[14] - M[12] * M[2] * M[7] + M[12] * M[3] * M[6];
	Inv[10] = M[0] * M[5] * M[15] - M[0] * M[7] * M[13] - M[4] * M[1] * M[15]
		+ M[4] * M[3] * M[13] + M[12] * M[1] * M[7] - M[12] * M[3] * M[5];
	Inv[14] = -M[0] * M[5] * M[14] + M[0] * M[6] * M[13] + M[4] * M[1] * M[14]
		- M[4] * M[2] * M[13] - M[12] * M[1] * M[6] + M[12] * M[2] * M[5];
	Inv[3] = -M[1] * M[6] * M[11] + M[1] * M[7] * M[10] + M[5] * M[2] * M[11]
		- M[5] * M[3] * M[10] - M[9] * M[2] * M[7] + M[9] * M[3] * M[6];
	Inv[7] = M[0] * M[6] * M[11] - M[0] * M[7] * M[10] - M[4] * M[2] * M[11]
		+ M[4] * M[3] * M[10] + M[8] * M[2] * M[7] - M[8] * M[3] * M[6];
	Inv[11] = -M[0] * M[5] * M[11] + M[0] * M[7] * M[9] + M[4] * M[1] * M[11]
		- M[4] * M[3] * M[9] - M[8] * M[1] * M[7] + M[8] * M[3] * M[5];
	Inv[15] = M[0] * M[5] * M[10] - M[0] * M[6] * M[9] - M[4] * M[1] * M[10]
		+ M[4] * M[2] * M[9] + M[8] * M[1] * M[6] - M[8] * M[2] * M[5];

	const float Det = M[0] * Inv[0] + M[1] * Inv[4] + M[2] * Inv[8] + M[3] * Inv[12];
	if (std::fabs(Det) < 1e-12f)
	{
		return false;
	}
	const float InvDet = 1.f / Det;
	for (int I = 0; I < 16; ++I)
	{
		Out[I] = Inv[I] * InvDet;
	}
	return true;
}

void IdentityMat4(float* Out)
{
	std::memset(Out, 0, sizeof(float) * 16);
	Out[0] = Out[5] = Out[10] = Out[15] = 1.f;
}

} // namespace

FSkeletonRenderProxy::FSkeletonRenderProxy(std::string InCatalogKey)
	: CatalogKey(std::move(InCatalogKey))
{
}

void FSkeletonRenderProxy::Release(FRHIResourceManager& Manager)
{
	if (IbmBuffer)
	{
		Manager.Release(IbmBuffer, true);
		IbmBuffer = nullptr;
	}
	BoneCount = 0;
	BoneNames.clear();
	ParentIndex.clear();
	InverseBindPose.clear();
	UploadedGeneration = 0;
	bReady = false;
}

void FSkeletonProxyRegistry::BeginUpload(
	FRHIServer& RHIServer,
	FSkeletonCpuSnapshot&& Snapshot,
	FTransferHandle Handle)
{
	if (Snapshot.CatalogKey.empty() || Snapshot.BoneCount == 0
		|| Snapshot.InverseBindPose.size() < static_cast<std::size_t>(Snapshot.BoneCount) * 16u)
	{
		SetTransferHandleState(Handle, ETransferState::Failed);
		return;
	}

	const std::string Key = Snapshot.CatalogKey;
	FSkeletonRenderProxy* Proxy = FindProxy(Key);
	if (!Proxy)
	{
		auto NewProxy = std::make_unique<FSkeletonRenderProxy>(Key);
		Proxy = NewProxy.get();
		Proxies.emplace(Key, std::move(NewProxy));
	}

	Proxy->BoneCount = Snapshot.BoneCount;
	Proxy->BoneNames = std::move(Snapshot.BoneNames);
	Proxy->ParentIndex = std::move(Snapshot.ParentIndex);
	Proxy->InverseBindPose = std::move(Snapshot.InverseBindPose);
	Proxy->UploadedGeneration = Snapshot.Generation;
	Proxy->bReady = true;

	if (!RHIServer.HasRHI() || !RHIServer.IsInitialized())
	{
		SetTransferHandleState(Handle, ETransferState::Succeeded);
		return;
	}

	FRHIServer* ServerPtr = &RHIServer;
	const std::uint64_t Generation = Snapshot.Generation;
	RHIServer.Enqueue(
		[this, ServerPtr, Proxy, Handle, Generation](FThreadedServer& /*Server*/)
		{
			IRHI* RHI = ServerPtr->GetRHI();
			if (!RHI || !Proxy)
			{
				SetTransferHandleState(Handle, ETransferState::Succeeded);
				return;
			}

			FRHIResourceManager& Manager = RHI->GetResourceManager();
			if (Proxy->IbmBuffer)
			{
				Manager.Release(Proxy->IbmBuffer, true);
				Proxy->IbmBuffer = nullptr;
			}

			FRHIBufferDesc Desc{};
			Desc.Size = Proxy->InverseBindPose.size() * sizeof(float);
			Desc.Usage = ERHIBufferUsage::Storage | ERHIBufferUsage::TransferDst;
			Desc.MemoryUsage = ERHIMemoryUsage::GPUOnly;
			Proxy->IbmBuffer = Manager.AcquireBuffer(Desc, Proxy->CatalogKey.c_str());
			if (!Proxy->IbmBuffer)
			{
				// CPU proxy remains Ready; GPU IBM is optional.
				SetTransferHandleState(Handle, ETransferState::Succeeded);
				return;
			}

			FRHIBufferDesc StagingDesc = Desc;
			StagingDesc.Usage = ERHIBufferUsage::TransferSrc;
			StagingDesc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
			FRHIBuffer* Staging = Manager.AcquireBuffer(StagingDesc);
			if (!Staging)
			{
				Manager.Release(Proxy->IbmBuffer, true);
				Proxy->IbmBuffer = nullptr;
				SetTransferHandleState(Handle, ETransferState::Succeeded);
				return;
			}

			RHI->UpdateBuffer(Staging, 0, Desc.Size, Proxy->InverseBindPose.data());
			FRHICommandList* CmdList = RHI->CreateCommandList(ERHICommandListType::Transfer);
			if (!CmdList)
			{
				Manager.Release(Staging, true);
				Manager.Release(Proxy->IbmBuffer, true);
				Proxy->IbmBuffer = nullptr;
				SetTransferHandleState(Handle, ETransferState::Succeeded);
				return;
			}

			CmdList->Begin();
			CmdList->TransitionBuffer(Proxy->IbmBuffer, ERHIResourceState::Common, ERHIResourceState::CopyDst);
			CmdList->CopyBuffer(Staging, 0, Proxy->IbmBuffer, 0, Desc.Size);
			CmdList->TransitionBuffer(
				Proxy->IbmBuffer,
				ERHIResourceState::CopyDst,
				ERHIResourceState::ShaderResource);
			CmdList->End();

			FRHIFence* Fence = RHI->CreateFence(false);
			FRHICommandList* Lists[] = { CmdList };
			RHI->GetTransferQueue().Submit(Lists, 1, nullptr, 0, nullptr, 0, Fence);

			FInFlightUpload Flight{};
			Flight.Handle = Handle;
			Flight.CatalogKey = Proxy->CatalogKey;
			Flight.Generation = Generation;
			Flight.Proxy = Proxy;
			Flight.Fence = Fence;
			Flight.Staging = Staging;
			Flight.CmdList = CmdList;
			Flight.bGpuPending = true;
			InFlight.push_back(std::move(Flight));
		});
}

void FSkeletonProxyRegistry::PollInFlight(FRHIServer& RHIServer)
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
				if (Flight.bGpuPending && Flight.Fence && !RHI->IsFenceSignaled(Flight.Fence))
				{
					StillInFlight.push_back(std::move(Flight));
					continue;
				}

				FRHIResourceManager& Manager = RHI->GetResourceManager();
				if (Flight.CmdList)
				{
					RHI->DestroyCommandList(Flight.CmdList);
				}
				if (Flight.Staging)
				{
					Manager.Release(Flight.Staging, true);
				}
				if (Flight.Fence)
				{
					RHI->DestroyFence(Flight.Fence);
				}
				SetTransferHandleState(Flight.Handle, ETransferState::Succeeded);
			}
			InFlight = std::move(StillInFlight);
		});
}

void FSkeletonProxyRegistry::Destroy(
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

	std::unique_ptr<FSkeletonRenderProxy> Owned = std::move(It->second);
	Proxies.erase(It);

	if (!RHIServer.HasRHI() || !RHIServer.IsInitialized())
	{
		SetTransferHandleState(Handle, ETransferState::Succeeded);
		return;
	}

	FRHIServer* ServerPtr = &RHIServer;
	FSkeletonRenderProxy* Proxy = Owned.release();
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

void FSkeletonProxyRegistry::DestroyAll(FRHIServer& RHIServer)
{
	std::vector<std::string> Keys;
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
	Proxies.clear();
	InFlight.clear();
}

void FSkeletonProxyRegistry::EnsureDefaultPlaceholder(FRHIServer& RHIServer)
{
	if (FindProxy(DefaultKey))
	{
		return;
	}

	FSkeletonCpuSnapshot Snap{};
	Snap.CatalogKey = DefaultKey;
	Snap.Generation = 1;
	Snap.BoneCount = 1;
	Snap.BoneNames = { "Root" };
	Snap.ParentIndex = { -1 };
	Snap.InverseBindPose.resize(16);
	IdentityMat4(Snap.InverseBindPose.data());

	FTransferHandle Handle = AllocateTransferHandle(ETransferState::InProgress);
	BeginUpload(RHIServer, std::move(Snap), Handle);
	for (int Attempt = 0; Attempt < 64; ++Attempt)
	{
		PollInFlight(RHIServer);
		if (RHIServer.HasRHI())
		{
			RHIServer.Flush();
		}
		if (Handle.HasSucceeded() || Handle.HasFailed())
		{
			return;
		}
	}
}

FSkeletonRenderProxy* FSkeletonProxyRegistry::FindProxy(const std::string& CatalogKey) const
{
	const auto It = Proxies.find(CatalogKey);
	return It == Proxies.end() ? nullptr : It->second.get();
}

FSkeletonRenderProxy* FSkeletonProxyRegistry::FindProxyOrDefault(const std::string& CatalogKey) const
{
	if (FSkeletonRenderProxy* Proxy = FindProxy(CatalogKey); Proxy && Proxy->IsReady())
	{
		return Proxy;
	}
	return GetDefaultProxy();
}

FSkeletonRenderProxy* FSkeletonProxyRegistry::GetDefaultProxy() const
{
	return FindProxy(DefaultKey);
}

} // namespace Maho
