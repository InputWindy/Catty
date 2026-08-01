#include "MeshRenderProxy.h"

#include <Core/Extension/Resource/Resource.h>
#include <Core/Object/SoftObjectPath.h>
#include <Core/System/Log.h>
#include <Render/RHI/RHI.h>
#include <Render/RHI/RHIResourceManager.h>
#include <Render/RHI/RHIServer.h>

#include <cstring>
#include <utility>

namespace Maho
{

namespace
{

constexpr std::uint32_t kMeshVertexStride = sizeof(float) * 8; // P3 N3 UV2

[[nodiscard]] bool IsSupportedMeshUpload(const FMeshCpuSnapshot& Snapshot)
{
	if (Snapshot.CatalogKey.empty() || Snapshot.VertexStride == 0 || Snapshot.VertexCount == 0)
	{
		return false;
	}
	if (Snapshot.Indices.empty())
	{
		return false;
	}
	const std::size_t ExpectedBytes =
		static_cast<std::size_t>(Snapshot.VertexCount) * static_cast<std::size_t>(Snapshot.VertexStride);
	return Snapshot.InterleavedVertices.size() >= ExpectedBytes;
}

} // namespace

bool TryBuildMeshCpuSnapshot(const UStaticMesh& Mesh, FMeshCpuSnapshot& Out)
{
	if (Mesh.GetLoadState() != EResourceLoadState::Ready)
	{
		return false;
	}

	const std::vector<float>& Positions = Mesh.GetPositions();
	const std::vector<float>& Normals = Mesh.GetNormals();
	const std::vector<float>& UVs = Mesh.GetUVs();
	const std::vector<std::uint32_t>& Indices = Mesh.GetIndices();
	if (Positions.size() < 3 || Indices.empty() || (Positions.size() % 3u) != 0)
	{
		return false;
	}

	const std::uint32_t VertexCount = static_cast<std::uint32_t>(Positions.size() / 3u);
	Out = FMeshCpuSnapshot{};
	Out.CatalogKey = FResourceSystem::MakeResourceCatalogKey(Mesh);
	Out.Generation = Mesh.GetContentGeneration();
	Out.VertexStride = kMeshVertexStride;
	Out.VertexCount = VertexCount;
	Out.bHasSkinning = false;
	Out.Indices = Indices;
	Out.InterleavedVertices.resize(static_cast<std::size_t>(VertexCount) * kMeshVertexStride);

	for (std::uint32_t Vi = 0; Vi < VertexCount; ++Vi)
	{
		float* Dst = reinterpret_cast<float*>(
			Out.InterleavedVertices.data() + static_cast<std::size_t>(Vi) * kMeshVertexStride);
		Dst[0] = Positions[Vi * 3u + 0u];
		Dst[1] = Positions[Vi * 3u + 1u];
		Dst[2] = Positions[Vi * 3u + 2u];
		if (Normals.size() >= (Vi + 1u) * 3u)
		{
			Dst[3] = Normals[Vi * 3u + 0u];
			Dst[4] = Normals[Vi * 3u + 1u];
			Dst[5] = Normals[Vi * 3u + 2u];
		}
		else
		{
			Dst[3] = 0.f;
			Dst[4] = 1.f;
			Dst[5] = 0.f;
		}
		if (UVs.size() >= (Vi + 1u) * 2u)
		{
			Dst[6] = UVs[Vi * 2u + 0u];
			Dst[7] = UVs[Vi * 2u + 1u];
		}
		else
		{
			Dst[6] = 0.f;
			Dst[7] = 0.f;
		}
	}

	return IsSupportedMeshUpload(Out);
}

FMeshRenderProxy::FMeshRenderProxy(std::string InCatalogKey)
	: CatalogKey(std::move(InCatalogKey))
{
}

void FMeshRenderProxy::Release(FRHIResourceManager& Manager)
{
	if (VertexBuffer)
	{
		Manager.Release(VertexBuffer, true);
		VertexBuffer = nullptr;
	}
	if (IndexBuffer)
	{
		Manager.Release(IndexBuffer, true);
		IndexBuffer = nullptr;
	}
	IndexCount = 0;
	VertexStride = 0;
	UploadedGeneration = 0;
	bHasSkinning = false;
	bReady = false;
}

void FMeshProxyRegistry::BeginUploadOnRHI(
	IRHI& RHI,
	FMeshRenderProxy& Proxy,
	FMeshCpuSnapshot& Snapshot,
	FInFlightUpload& OutFlight)
{
	const FTransferHandle KeptHandle = OutFlight.Handle;
	OutFlight = FInFlightUpload{};
	OutFlight.Handle = KeptHandle;
	OutFlight.CatalogKey = Snapshot.CatalogKey;
	OutFlight.Generation = Snapshot.Generation;
	OutFlight.Proxy = &Proxy;
	OutFlight.IndexCount = static_cast<std::uint32_t>(Snapshot.Indices.size());
	OutFlight.VertexStride = Snapshot.VertexStride;
	OutFlight.bHasSkinning = Snapshot.bHasSkinning;

	if (!IsSupportedMeshUpload(Snapshot))
	{
		MAHO_CORE_ERROR("MeshProxy: unsupported snapshot '{}'", Snapshot.CatalogKey);
		return;
	}

	FRHIResourceManager& Manager = RHI.GetResourceManager();
	if (Proxy.VertexBuffer && Snapshot.Generation <= Proxy.UploadedGeneration && Proxy.bReady)
	{
		return;
	}

	Proxy.Release(Manager);

	FRHIBufferDesc VbDesc{};
	VbDesc.Size = Snapshot.InterleavedVertices.size();
	VbDesc.Usage = ERHIBufferUsage::Vertex | ERHIBufferUsage::TransferDst;
	VbDesc.MemoryUsage = ERHIMemoryUsage::GPUOnly;
	Proxy.VertexBuffer = Manager.AcquireBuffer(VbDesc, Snapshot.CatalogKey.c_str());
	if (!Proxy.VertexBuffer)
	{
		MAHO_CORE_ERROR("MeshProxy: AcquireBuffer VB failed '{}'", Snapshot.CatalogKey);
		return;
	}

	FRHIBufferDesc IbDesc{};
	IbDesc.Size = Snapshot.Indices.size() * sizeof(std::uint32_t);
	IbDesc.Usage = ERHIBufferUsage::Index | ERHIBufferUsage::TransferDst;
	IbDesc.MemoryUsage = ERHIMemoryUsage::GPUOnly;
	Proxy.IndexBuffer = Manager.AcquireBuffer(IbDesc, (Snapshot.CatalogKey + ".ib").c_str());
	if (!Proxy.IndexBuffer)
	{
		Manager.Release(Proxy.VertexBuffer, true);
		Proxy.VertexBuffer = nullptr;
		MAHO_CORE_ERROR("MeshProxy: AcquireBuffer IB failed '{}'", Snapshot.CatalogKey);
		return;
	}

	FRHIBufferDesc StagingVbDesc = VbDesc;
	StagingVbDesc.Usage = ERHIBufferUsage::TransferSrc;
	StagingVbDesc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
	FRHIBuffer* StagingVB = Manager.AcquireBuffer(StagingVbDesc);
	FRHIBufferDesc StagingIbDesc = IbDesc;
	StagingIbDesc.Usage = ERHIBufferUsage::TransferSrc;
	StagingIbDesc.MemoryUsage = ERHIMemoryUsage::CPUToGPU;
	FRHIBuffer* StagingIB = Manager.AcquireBuffer(StagingIbDesc);
	if (!StagingVB || !StagingIB)
	{
		if (StagingVB)
		{
			Manager.Release(StagingVB, true);
		}
		if (StagingIB)
		{
			Manager.Release(StagingIB, true);
		}
		Proxy.Release(Manager);
		MAHO_CORE_ERROR("MeshProxy: staging acquire failed '{}'", Snapshot.CatalogKey);
		return;
	}

	RHI.UpdateBuffer(StagingVB, 0, VbDesc.Size, Snapshot.InterleavedVertices.data());
	RHI.UpdateBuffer(StagingIB, 0, IbDesc.Size, Snapshot.Indices.data());

	FRHICommandList* CmdList = RHI.CreateCommandList(ERHICommandListType::Transfer);
	if (!CmdList)
	{
		Manager.Release(StagingVB, true);
		Manager.Release(StagingIB, true);
		Proxy.Release(Manager);
		MAHO_CORE_ERROR("MeshProxy: CreateCommandList failed '{}'", Snapshot.CatalogKey);
		return;
	}

	CmdList->Begin();
	CmdList->TransitionBuffer(Proxy.VertexBuffer, ERHIResourceState::Common, ERHIResourceState::CopyDst);
	CmdList->TransitionBuffer(Proxy.IndexBuffer, ERHIResourceState::Common, ERHIResourceState::CopyDst);
	CmdList->CopyBuffer(StagingVB, 0, Proxy.VertexBuffer, 0, VbDesc.Size);
	CmdList->CopyBuffer(StagingIB, 0, Proxy.IndexBuffer, 0, IbDesc.Size);
	CmdList->TransitionBuffer(Proxy.VertexBuffer, ERHIResourceState::CopyDst, ERHIResourceState::VertexBuffer);
	CmdList->TransitionBuffer(Proxy.IndexBuffer, ERHIResourceState::CopyDst, ERHIResourceState::IndexBuffer);
	CmdList->End();

	FRHIFence* Fence = RHI.CreateFence(false);
	FRHICommandList* Lists[] = { CmdList };
	RHI.GetTransferQueue().Submit(Lists, 1, nullptr, 0, nullptr, 0, Fence);

	OutFlight.Fence = Fence;
	OutFlight.StagingVB = StagingVB;
	OutFlight.StagingIB = StagingIB;
	OutFlight.CmdList = CmdList;
}

void FMeshProxyRegistry::CompleteUploadOnRHI(IRHI& RHI, FInFlightUpload& Flight, bool bSuccess)
{
	FRHIResourceManager& Manager = RHI.GetResourceManager();
	if (Flight.CmdList)
	{
		RHI.DestroyCommandList(Flight.CmdList);
		Flight.CmdList = nullptr;
	}
	if (Flight.StagingVB)
	{
		Manager.Release(Flight.StagingVB, true);
		Flight.StagingVB = nullptr;
	}
	if (Flight.StagingIB)
	{
		Manager.Release(Flight.StagingIB, true);
		Flight.StagingIB = nullptr;
	}
	if (Flight.Fence)
	{
		RHI.DestroyFence(Flight.Fence);
		Flight.Fence = nullptr;
	}

	if (bSuccess && Flight.Proxy)
	{
		Flight.Proxy->IndexCount = Flight.IndexCount;
		Flight.Proxy->VertexStride = Flight.VertexStride;
		Flight.Proxy->bHasSkinning = Flight.bHasSkinning;
		Flight.Proxy->UploadedGeneration = Flight.Generation;
		Flight.Proxy->bReady = Flight.Proxy->VertexBuffer && Flight.Proxy->IndexBuffer;
		SetTransferHandleState(Flight.Handle, ETransferState::Succeeded);
	}
	else
	{
		if (Flight.Proxy)
		{
			Flight.Proxy->Release(Manager);
		}
		SetTransferHandleState(Flight.Handle, ETransferState::Failed);
	}
}

void FMeshProxyRegistry::BeginUpload(
	FRHIServer& RHIServer,
	FMeshCpuSnapshot&& Snapshot,
	FTransferHandle Handle)
{
	if (!RHIServer.HasRHI() || !RHIServer.IsInitialized())
	{
		SetTransferHandleState(Handle, ETransferState::Failed);
		return;
	}
	if (!IsSupportedMeshUpload(Snapshot))
	{
		SetTransferHandleState(Handle, ETransferState::Failed);
		return;
	}

	const std::string Key = Snapshot.CatalogKey;
	FMeshRenderProxy* Proxy = FindProxy(Key);
	if (!Proxy)
	{
		auto NewProxy = std::make_unique<FMeshRenderProxy>(Key);
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

void FMeshProxyRegistry::PollInFlight(FRHIServer& RHIServer)
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

void FMeshProxyRegistry::Destroy(
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

	std::unique_ptr<FMeshRenderProxy> Owned = std::move(It->second);
	Proxies.erase(It);

	if (!RHIServer.HasRHI() || !RHIServer.IsInitialized())
	{
		SetTransferHandleState(Handle, ETransferState::Succeeded);
		return;
	}

	FRHIServer* ServerPtr = &RHIServer;
	FMeshRenderProxy* Proxy = Owned.release();
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

void FMeshProxyRegistry::DestroyAll(FRHIServer& RHIServer)
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

	const auto DefaultIt = Proxies.find(DefaultKey);
	if (DefaultIt != Proxies.end() && DefaultIt->second && RHIServer.HasRHI() && RHIServer.IsInitialized())
	{
		FRHIServer* ServerPtr = &RHIServer;
		FMeshRenderProxy* Proxy = DefaultIt->second.get();
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
	Proxies.clear();
	InFlight.clear();
}

void FMeshProxyRegistry::EnsureDefaultPlaceholder(FRHIServer& RHIServer)
{
	if (FindProxy(DefaultKey))
	{
		return;
	}

	// Unit triangle in XY, +Z normal.
	FMeshCpuSnapshot Snap{};
	Snap.CatalogKey = DefaultKey;
	Snap.Generation = 1;
	Snap.VertexStride = kMeshVertexStride;
	Snap.VertexCount = 3;
	Snap.bHasSkinning = false;
	Snap.Indices = { 0, 1, 2 };
	Snap.InterleavedVertices.resize(3u * kMeshVertexStride);
	float* V = reinterpret_cast<float*>(Snap.InterleavedVertices.data());
	const float Verts[] = {
		0.f, 0.5f, 0.f, 0.f, 0.f, 1.f, 0.5f, 1.f,
		-0.5f, -0.5f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f,
		0.5f, -0.5f, 0.f, 0.f, 0.f, 1.f, 1.f, 0.f,
	};
	std::memcpy(V, Verts, sizeof(Verts));

	FTransferHandle Handle = AllocateTransferHandle(ETransferState::InProgress);
	BeginUpload(RHIServer, std::move(Snap), Handle);
	for (int Attempt = 0; Attempt < 64; ++Attempt)
	{
		PollInFlight(RHIServer);
		RHIServer.Flush();
		if (FMeshRenderProxy* Proxy = FindProxy(DefaultKey); Proxy && Proxy->IsReady())
		{
			return;
		}
		if (Handle.HasFailed() || Handle.HasSucceeded())
		{
			return;
		}
	}
}

FMeshRenderProxy* FMeshProxyRegistry::FindProxy(const std::string& CatalogKey) const
{
	const auto It = Proxies.find(CatalogKey);
	return It == Proxies.end() ? nullptr : It->second.get();
}

FMeshRenderProxy* FMeshProxyRegistry::FindProxyOrDefault(const std::string& CatalogKey) const
{
	if (FMeshRenderProxy* Proxy = FindProxy(CatalogKey); Proxy && Proxy->IsReady())
	{
		return Proxy;
	}
	return GetDefaultProxy();
}

FMeshRenderProxy* FMeshProxyRegistry::GetDefaultProxy() const
{
	return FindProxy(DefaultKey);
}

} // namespace Maho
