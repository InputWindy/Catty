#include <Render/RenderServer.h>
#include <Render/RenderCommand.h>

#include <Core/Application/App.h>
#include <Core/Extension/Render/Render.h>
#include <Core/Extension/Resource/Resource.h>
#include <Core/Object/SoftObjectPath.h>
#include <Core/System/Console.h>
#include <Core/System/Log.h>
#include "Render/AnimationRenderProxy.h"
#include "Render/MeshRenderProxy.h"
#include "Render/SkeletonRenderProxy.h"
#include "Render/TextureRenderProxy.h"
#include "Render/UI/ImGuiDrawDataRing.h"

#include <algorithm>
#include <utility>

namespace Maho
{

namespace
{

static TAutoConsoleVariable GCVarMaxFramesInFlight(
	"r.MaxFramesInFlight",
	3,
	"Max Game frames submitted to RHI before waiting (1..3)");

static_assert(
	FRenderServer::MaxFramesInFlightCap == ImGuiDrawDataRingSlotCount,
	"ImGui draw-data ring slot count must match MaxFramesInFlightCap");

} // namespace

namespace Detail
{

FRenderServer* GetRenderServer()
{
	if (!GApp)
	{
		return nullptr;
	}
	FRenderSystem* System = GApp->GetExtension<FRenderSystem>();
	return System ? &System->GetRenderServer() : nullptr;
}

} // namespace Detail

// ---------------------------------------------------------------------------
// TRenderResourceExporter specializations (Game-thread Submit)
// ---------------------------------------------------------------------------

template <>
struct TRenderResourceExporter<UTexture>
{
	static FTransferHandle Submit(FRenderServer& Server, const UTexture& Texture)
	{
		FTextureCpuSnapshot Snap;
		if (!TryBuildTextureCpuSnapshot(Texture, Snap))
		{
			return AllocateTransferHandle(ETransferState::Failed);
		}
		FTransferHandle Handle = AllocateTransferHandle(ETransferState::InProgress);
		Server.PushPendingTextureUpload(std::move(Snap), Handle);
		return Handle;
	}

	static FTransferHandle SubmitDestroy(FRenderServer& Server, const UTexture& Texture)
	{
		FTransferHandle Handle = AllocateTransferHandle(ETransferState::InProgress);
		Server.PushPendingTextureDestroy(FResourceSystem::MakeResourceCatalogKey(Texture), Handle);
		return Handle;
	}
};

template <>
struct TRenderResourceExporter<UTexture2D>
{
	static FTransferHandle Submit(FRenderServer& Server, const UTexture2D& Texture)
	{
		return TRenderResourceExporter<UTexture>::Submit(Server, Texture);
	}
	static FTransferHandle SubmitDestroy(FRenderServer& Server, const UTexture2D& Texture)
	{
		return TRenderResourceExporter<UTexture>::SubmitDestroy(Server, Texture);
	}
};

template <>
struct TRenderResourceExporter<UTexture3D>
{
	static FTransferHandle Submit(FRenderServer& Server, const UTexture3D& Texture)
	{
		return TRenderResourceExporter<UTexture>::Submit(Server, Texture);
	}
	static FTransferHandle SubmitDestroy(FRenderServer& Server, const UTexture3D& Texture)
	{
		return TRenderResourceExporter<UTexture>::SubmitDestroy(Server, Texture);
	}
};

template <>
struct TRenderResourceExporter<UTextureCube>
{
	static FTransferHandle Submit(FRenderServer& Server, const UTextureCube& Texture)
	{
		return TRenderResourceExporter<UTexture>::Submit(Server, Texture);
	}
	static FTransferHandle SubmitDestroy(FRenderServer& Server, const UTextureCube& Texture)
	{
		return TRenderResourceExporter<UTexture>::SubmitDestroy(Server, Texture);
	}
};

template <>
struct TRenderResourceExporter<UTextureCubeArray>
{
	static FTransferHandle Submit(FRenderServer& Server, const UTextureCubeArray& Texture)
	{
		return TRenderResourceExporter<UTexture>::Submit(Server, Texture);
	}
	static FTransferHandle SubmitDestroy(FRenderServer& Server, const UTextureCubeArray& Texture)
	{
		return TRenderResourceExporter<UTexture>::SubmitDestroy(Server, Texture);
	}
};

template <>
struct TRenderResourceExporter<UTexture2DArray>
{
	static FTransferHandle Submit(FRenderServer& Server, const UTexture2DArray& Texture)
	{
		return TRenderResourceExporter<UTexture>::Submit(Server, Texture);
	}
	static FTransferHandle SubmitDestroy(FRenderServer& Server, const UTexture2DArray& Texture)
	{
		return TRenderResourceExporter<UTexture>::SubmitDestroy(Server, Texture);
	}
};

template <>
struct TRenderResourceExporter<UStaticMesh>
{
	static FTransferHandle Submit(FRenderServer& Server, const UStaticMesh& Mesh)
	{
		FMeshCpuSnapshot Snap;
		if (!TryBuildMeshCpuSnapshot(Mesh, Snap))
		{
			return AllocateTransferHandle(ETransferState::Failed);
		}
		FTransferHandle Handle = AllocateTransferHandle(ETransferState::InProgress);
		Server.PushPendingMeshUpload(std::move(Snap), Handle);
		return Handle;
	}

	static FTransferHandle SubmitDestroy(FRenderServer& Server, const UStaticMesh& Mesh)
	{
		FTransferHandle Handle = AllocateTransferHandle(ETransferState::InProgress);
		Server.PushPendingMeshDestroy(FResourceSystem::MakeResourceCatalogKey(Mesh), Handle);
		return Handle;
	}
};

template <>
struct TRenderResourceExporter<USkeleton>
{
	static FTransferHandle Submit(FRenderServer& Server, const USkeleton& Skeleton)
	{
		FSkeletonCpuSnapshot Snap;
		if (!TryBuildSkeletonCpuSnapshot(Skeleton, Snap))
		{
			return AllocateTransferHandle(ETransferState::Failed);
		}
		FTransferHandle Handle = AllocateTransferHandle(ETransferState::InProgress);
		Server.PushPendingSkeletonUpload(std::move(Snap), Handle);
		return Handle;
	}

	static FTransferHandle SubmitDestroy(FRenderServer& Server, const USkeleton& Skeleton)
	{
		FTransferHandle Handle = AllocateTransferHandle(ETransferState::InProgress);
		Server.PushPendingSkeletonDestroy(FResourceSystem::MakeResourceCatalogKey(Skeleton), Handle);
		return Handle;
	}
};

template <>
struct TRenderResourceExporter<UAnimation>
{
	static FTransferHandle Submit(FRenderServer& Server, const UAnimation& Animation)
	{
		FAnimationCpuSnapshot Snap;
		if (!TryBuildAnimationCpuSnapshot(Animation, Snap))
		{
			return AllocateTransferHandle(ETransferState::Failed);
		}
		FTransferHandle Handle = AllocateTransferHandle(ETransferState::InProgress);
		Server.PushPendingAnimationUpload(std::move(Snap), Handle);
		return Handle;
	}

	static FTransferHandle SubmitDestroy(FRenderServer& Server, const UAnimation& Animation)
	{
		FTransferHandle Handle = AllocateTransferHandle(ETransferState::InProgress);
		Server.PushPendingAnimationDestroy(FResourceSystem::MakeResourceCatalogKey(Animation), Handle);
		return Handle;
	}
};

template <typename TResource>
FTransferHandle FRenderServer::QueueResourceUpload(const TResource& Resource)
{
	static_assert(std::is_base_of_v<UResource, TResource>,
		"QueueResourceUpload requires TResource : UResource");
	return TRenderResourceExporter<TResource>::Submit(*this, Resource);
}

template <typename TResource>
FTransferHandle FRenderServer::RequestResourceDestroy(const TResource& Resource)
{
	static_assert(std::is_base_of_v<UResource, TResource>,
		"RequestResourceDestroy requires TResource : UResource");
	return TRenderResourceExporter<TResource>::SubmitDestroy(*this, Resource);
}

template FTransferHandle FRenderServer::QueueResourceUpload<UTexture>(const UTexture&);
template FTransferHandle FRenderServer::QueueResourceUpload<UTexture2D>(const UTexture2D&);
template FTransferHandle FRenderServer::QueueResourceUpload<UTexture3D>(const UTexture3D&);
template FTransferHandle FRenderServer::QueueResourceUpload<UTextureCube>(const UTextureCube&);
template FTransferHandle FRenderServer::QueueResourceUpload<UTextureCubeArray>(const UTextureCubeArray&);
template FTransferHandle FRenderServer::QueueResourceUpload<UTexture2DArray>(const UTexture2DArray&);
template FTransferHandle FRenderServer::QueueResourceUpload<UStaticMesh>(const UStaticMesh&);
template FTransferHandle FRenderServer::QueueResourceUpload<USkeleton>(const USkeleton&);
template FTransferHandle FRenderServer::QueueResourceUpload<UAnimation>(const UAnimation&);

template FTransferHandle FRenderServer::RequestResourceDestroy<UTexture>(const UTexture&);
template FTransferHandle FRenderServer::RequestResourceDestroy<UTexture2D>(const UTexture2D&);
template FTransferHandle FRenderServer::RequestResourceDestroy<UTexture3D>(const UTexture3D&);
template FTransferHandle FRenderServer::RequestResourceDestroy<UTextureCube>(const UTextureCube&);
template FTransferHandle FRenderServer::RequestResourceDestroy<UTextureCubeArray>(const UTextureCubeArray&);
template FTransferHandle FRenderServer::RequestResourceDestroy<UTexture2DArray>(const UTexture2DArray&);
template FTransferHandle FRenderServer::RequestResourceDestroy<UStaticMesh>(const UStaticMesh&);
template FTransferHandle FRenderServer::RequestResourceDestroy<USkeleton>(const USkeleton&);
template FTransferHandle FRenderServer::RequestResourceDestroy<UAnimation>(const UAnimation&);

// ---------------------------------------------------------------------------
// FRenderServer
// ---------------------------------------------------------------------------

FRenderServer::FRenderServer()
	: ImGuiDrawDataRing(std::make_unique<FImGuiDrawDataRing>())
	, TextureProxies(std::make_unique<FTextureProxyRegistry>())
	, MeshProxies(std::make_unique<FMeshProxyRegistry>())
	, SkeletonProxies(std::make_unique<FSkeletonProxyRegistry>())
	, AnimationProxies(std::make_unique<FAnimationProxyRegistry>())
{
}

FRenderServer::~FRenderServer()
{
	TearDown();
}

int FRenderServer::GetMaxFramesInFlight() const
{
	const int Requested = GCVarMaxFramesInFlight.GetValue();
	return (std::clamp)(Requested, 1, MaxFramesInFlightCap);
}

void FRenderServer::SetClearColor(float R, float G, float B, float A)
{
	ClearColorR = R;
	ClearColorG = G;
	ClearColorB = B;
	ClearColorA = A;
}

FTextureProxyRegistry& FRenderServer::GetTextureProxyRegistry()
{
	return *TextureProxies;
}

const FTextureProxyRegistry& FRenderServer::GetTextureProxyRegistry() const
{
	return *TextureProxies;
}

FMeshProxyRegistry& FRenderServer::GetMeshProxyRegistry()
{
	return *MeshProxies;
}

const FMeshProxyRegistry& FRenderServer::GetMeshProxyRegistry() const
{
	return *MeshProxies;
}

FSkeletonProxyRegistry& FRenderServer::GetSkeletonProxyRegistry()
{
	return *SkeletonProxies;
}

const FSkeletonProxyRegistry& FRenderServer::GetSkeletonProxyRegistry() const
{
	return *SkeletonProxies;
}

FAnimationProxyRegistry& FRenderServer::GetAnimationProxyRegistry()
{
	return *AnimationProxies;
}

const FAnimationProxyRegistry& FRenderServer::GetAnimationProxyRegistry() const
{
	return *AnimationProxies;
}

void FRenderServer::PushPendingTextureUpload(FTextureCpuSnapshot Snapshot, FTransferHandle Handle)
{
	std::lock_guard<std::mutex> Lock(PendingUploadMutex);
	PendingTextureUploads.push_back(FPendingTextureUpload{ std::move(Snapshot), Handle });
}

void FRenderServer::PushPendingTextureDestroy(std::string CatalogKey, FTransferHandle Handle)
{
	std::lock_guard<std::mutex> Lock(PendingUploadMutex);
	PendingTextureDestroys.push_back(FPendingDestroy{ std::move(CatalogKey), Handle });
}

void FRenderServer::PushPendingMeshUpload(FMeshCpuSnapshot Snapshot, FTransferHandle Handle)
{
	std::lock_guard<std::mutex> Lock(PendingUploadMutex);
	PendingMeshUploads.push_back(FPendingMeshUpload{ std::move(Snapshot), Handle });
}

void FRenderServer::PushPendingMeshDestroy(std::string CatalogKey, FTransferHandle Handle)
{
	std::lock_guard<std::mutex> Lock(PendingUploadMutex);
	PendingMeshDestroys.push_back(FPendingDestroy{ std::move(CatalogKey), Handle });
}

void FRenderServer::PushPendingSkeletonUpload(FSkeletonCpuSnapshot Snapshot, FTransferHandle Handle)
{
	std::lock_guard<std::mutex> Lock(PendingUploadMutex);
	PendingSkeletonUploads.push_back(FPendingSkeletonUpload{ std::move(Snapshot), Handle });
}

void FRenderServer::PushPendingSkeletonDestroy(std::string CatalogKey, FTransferHandle Handle)
{
	std::lock_guard<std::mutex> Lock(PendingUploadMutex);
	PendingSkeletonDestroys.push_back(FPendingDestroy{ std::move(CatalogKey), Handle });
}

void FRenderServer::PushPendingAnimationUpload(FAnimationCpuSnapshot Snapshot, FTransferHandle Handle)
{
	std::lock_guard<std::mutex> Lock(PendingUploadMutex);
	PendingAnimationUploads.push_back(FPendingAnimationUpload{ std::move(Snapshot), Handle });
}

void FRenderServer::PushPendingAnimationDestroy(std::string CatalogKey, FTransferHandle Handle)
{
	std::lock_guard<std::mutex> Lock(PendingUploadMutex);
	PendingAnimationDestroys.push_back(FPendingDestroy{ std::move(CatalogKey), Handle });
}

bool FRenderServer::Boot(FPlatformWindow& InWindow, const FConfig& Config)
{
	BoundWindow = &InWindow;
	ClearColorR = Config.ClearColorR;
	ClearColorG = Config.ClearColorG;
	ClearColorB = Config.ClearColorB;
	ClearColorA = Config.ClearColorA;

	if (!Initialize())
	{
		MAHO_CORE_ERROR("RenderServer: MahoRender Initialize failed");
		BoundWindow = nullptr;
		return false;
	}

	if (!RHIServer.Initialize())
	{
		MAHO_CORE_ERROR("RenderServer: RHIServer Initialize failed");
		Shutdown();
		BoundWindow = nullptr;
		return false;
	}

	if (!RHIServer.InitializeRHI(InWindow))
	{
		MAHO_CORE_ERROR("RenderServer: InitializeRHI failed");
		RHIServer.Shutdown();
		Shutdown();
		BoundWindow = nullptr;
		return false;
	}

	TextureProxies->EnsureDefaultPlaceholder(RHIServer);
	MeshProxies->EnsureDefaultPlaceholder(RHIServer);
	SkeletonProxies->EnsureDefaultPlaceholder(RHIServer);
	AnimationProxies->EnsureDefaultPlaceholder();

	if (InWindow.HasOsWindow())
	{
		InWindow.GetFramebufferSize(LastFramebufferWidth, LastFramebufferHeight);
		if (!ImGui.Initialize(InWindow, RHIServer, Config.ProjectConfigDir))
		{
			MAHO_CORE_ERROR("RenderServer: ImGui Initialize failed");
			RHIServer.Shutdown();
			Shutdown();
			BoundWindow = nullptr;
			return false;
		}
		SetImGuiEnabled(true);
	}

	RHIServer.ResetFrameFence();
	CurrentFrameIndex = 0;

	MAHO_CORE_INFO("RenderServer: Boot ok (MaxFramesInFlight={})", GetMaxFramesInFlight());
	return true;
}

void FRenderServer::TearDown()
{
	if (IsInitialized())
	{
		Flush();
	}

	if (AnimationProxies)
	{
		AnimationProxies->DestroyAll(RHIServer);
	}
	if (SkeletonProxies && RHIServer.IsInitialized())
	{
		SkeletonProxies->DestroyAll(RHIServer);
	}
	if (MeshProxies && RHIServer.IsInitialized())
	{
		MeshProxies->DestroyAll(RHIServer);
	}
	if (TextureProxies && RHIServer.IsInitialized())
	{
		TextureProxies->DestroyAll(RHIServer);
	}

	if (RHIServer.IsInitialized())
	{
		RHIServer.Flush();
	}

	SetImGuiEnabled(false);
	if (ImGui.IsInitialized())
	{
		ImGui.Shutdown(RHIServer);
	}
	if (RHIServer.IsInitialized())
	{
		RHIServer.Shutdown();
	}
	if (ImGuiDrawDataRing)
	{
		ImGuiDrawDataRing->ReleaseAll();
	}

	RenderExtensions.clear();
	{
		std::lock_guard<std::mutex> Lock(PendingUploadMutex);
		PendingTextureUploads.clear();
		PendingTextureDestroys.clear();
		PendingMeshUploads.clear();
		PendingMeshDestroys.clear();
		PendingSkeletonUploads.clear();
		PendingSkeletonDestroys.clear();
		PendingAnimationUploads.clear();
		PendingAnimationDestroys.clear();
	}

	if (IsInitialized())
	{
		Shutdown();
	}

	BoundWindow = nullptr;
	LastFramebufferWidth = 0;
	LastFramebufferHeight = 0;
	CurrentFrameIndex = 0;
}

void FRenderServer::WaitBeforeImGuiNewFrame(std::uint64_t FrameIndex)
{
	if (FrameIndex > 1)
	{
		RHIServer.WaitForRenderFrame(FrameIndex - 1);
	}
}

bool FRenderServer::InvokeRenderStage(
	IRenderExtension& Extension,
	ERenderStage Stage,
	FRenderServer& Self)
{
	Extension.SetCurrentStage(Stage);
	return Extension.ExecuteStage(Stage, Self);
}

void FRenderServer::ExecuteRenderStages(const FRenderFramePacket& Packet)
{
	static constexpr ERenderStage Stages[] =
	{
		ERenderStage::BeginFrame,
		ERenderStage::ProcessPacket,
		ERenderStage::Cull,
		ERenderStage::BuildDrawLists,
		ERenderStage::UploadPrep,
		ERenderStage::KickRHI,
		ERenderStage::EndFrame,
	};

	for (ERenderStage Stage : Stages)
	{
		for (std::unique_ptr<IRenderExtension>& Extension : RenderExtensions)
		{
			if (!Extension)
			{
				continue;
			}
			if (!InvokeRenderStage(*Extension, Stage, *this))
			{
				MAHO_CORE_ERROR(
					"FRenderServer: render extension '{}' failed at stage {}",
					Extension->GetName() ? Extension->GetName() : "?",
					static_cast<int>(Stage));
				return;
			}
		}

		if (Stage == ERenderStage::KickRHI)
		{
			RHIServer.SubmitBeginMainPass(
				Packet.ClearColorR,
				Packet.ClearColorG,
				Packet.ClearColorB,
				Packet.ClearColorA);
			if (Packet.bSubmitImGui && Packet.ImGuiSlotIndex >= 0 && ImGuiDrawDataRing)
			{
				RHIServer.SubmitRenderUI(*ImGuiDrawDataRing, Packet.ImGuiSlotIndex);
			}
			RHIServer.SubmitEndFrameAndFence(Packet.FrameIndex);
			RHIServer.Flush();
		}
	}
}

void FRenderServer::ProcessPendingResourceTransfers()
{
	std::vector<FPendingTextureUpload> TextureUploads;
	std::vector<FPendingDestroy> TextureDestroys;
	std::vector<FPendingMeshUpload> MeshUploads;
	std::vector<FPendingDestroy> MeshDestroys;
	std::vector<FPendingSkeletonUpload> SkeletonUploads;
	std::vector<FPendingDestroy> SkeletonDestroys;
	std::vector<FPendingAnimationUpload> AnimationUploads;
	std::vector<FPendingDestroy> AnimationDestroys;
	{
		std::lock_guard<std::mutex> Lock(PendingUploadMutex);
		TextureUploads.swap(PendingTextureUploads);
		TextureDestroys.swap(PendingTextureDestroys);
		MeshUploads.swap(PendingMeshUploads);
		MeshDestroys.swap(PendingMeshDestroys);
		SkeletonUploads.swap(PendingSkeletonUploads);
		SkeletonDestroys.swap(PendingSkeletonDestroys);
		AnimationUploads.swap(PendingAnimationUploads);
		AnimationDestroys.swap(PendingAnimationDestroys);
	}

	for (FPendingDestroy& Item : TextureDestroys)
	{
		TextureProxies->Destroy(RHIServer, Item.CatalogKey, Item.Handle);
	}
	for (FPendingTextureUpload& Item : TextureUploads)
	{
		TextureProxies->BeginUpload(RHIServer, std::move(Item.Snapshot), Item.Handle);
	}
	TextureProxies->PollInFlight(RHIServer);

	for (FPendingDestroy& Item : MeshDestroys)
	{
		MeshProxies->Destroy(RHIServer, Item.CatalogKey, Item.Handle);
	}
	for (FPendingMeshUpload& Item : MeshUploads)
	{
		MeshProxies->BeginUpload(RHIServer, std::move(Item.Snapshot), Item.Handle);
	}
	MeshProxies->PollInFlight(RHIServer);

	for (FPendingDestroy& Item : SkeletonDestroys)
	{
		SkeletonProxies->Destroy(RHIServer, Item.CatalogKey, Item.Handle);
	}
	for (FPendingSkeletonUpload& Item : SkeletonUploads)
	{
		SkeletonProxies->BeginUpload(RHIServer, std::move(Item.Snapshot), Item.Handle);
	}
	SkeletonProxies->PollInFlight(RHIServer);

	for (FPendingDestroy& Item : AnimationDestroys)
	{
		AnimationProxies->Destroy(RHIServer, Item.CatalogKey, Item.Handle);
	}
	for (FPendingAnimationUpload& Item : AnimationUploads)
	{
		AnimationProxies->BeginUpload(RHIServer, std::move(Item.Snapshot), Item.Handle);
	}
}

void FRenderServer::ExecuteFrame(FRenderFramePacket Packet)
{
	CurrentFrameIndex = Packet.FrameIndex;

	if (Packet.bResizeFramebuffer && Packet.FramebufferWidth > 0 && Packet.FramebufferHeight > 0)
	{
		LastFramebufferWidth = Packet.FramebufferWidth;
		LastFramebufferHeight = Packet.FramebufferHeight;
		RHIServer.RequestResize(Packet.FramebufferWidth, Packet.FramebufferHeight);
	}

	ProcessPendingResourceTransfers();
	ExecuteRenderStages(Packet);
}

void FRenderServer::Render(std::uint64_t FrameIndex)
{
	const int MaxInFlight = GetMaxFramesInFlight();
	if (FrameIndex > static_cast<std::uint64_t>(MaxInFlight))
	{
		RHIServer.WaitForRenderFrame(FrameIndex - static_cast<std::uint64_t>(MaxInFlight));
	}

	FRenderFramePacket Packet{};
	Packet.FrameIndex = FrameIndex;
	Packet.ClearColorR = ClearColorR;
	Packet.ClearColorG = ClearColorG;
	Packet.ClearColorB = ClearColorB;
	Packet.ClearColorA = ClearColorA;

	int Width = LastFramebufferWidth;
	int Height = LastFramebufferHeight;
	if (BoundWindow && BoundWindow->HasOsWindow() && HasRHI())
	{
		BoundWindow->GetFramebufferSize(Width, Height);
		if (Width > 0 && Height > 0
			&& (Width != LastFramebufferWidth || Height != LastFramebufferHeight))
		{
			Packet.bResizeFramebuffer = true;
			Packet.FramebufferWidth = Width;
			Packet.FramebufferHeight = Height;
		}
	}

	Packet.ImGuiSlotIndex = -1;
	Packet.bSubmitImGui = false;
	if (ImGui.IsInitialized())
	{
		ImGui.EndFrame();
		if (ImGuiDrawDataRing)
		{
			Packet.ImGuiSlotIndex = ImGuiDrawDataRing->CaptureFromImGui(FrameIndex);
			Packet.bSubmitImGui = bImGuiEnabled && Packet.ImGuiSlotIndex >= 0;
		}
	}

	ENQUEUE_RENDER_COMMAND(RenderFrame)(
		[Packet = std::move(Packet)](FRenderServer& Server) mutable
		{
			Server.ExecuteFrame(std::move(Packet));
		});
	Flush();

	if (ImGui.IsInitialized())
	{
		ImGui.UpdateAndRenderPlatformWindows();
	}
}

void FRenderServer::SyncFramebufferSize()
{
	// Framebuffer sync is performed on Game in Render() into FRenderFramePacket.
}

} // namespace Maho
