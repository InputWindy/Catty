#include <Render/RenderServer.h>
#include <Render/RenderCommand.h>

#include <Core/Application/App.h>
#include <Core/Extension/Render/Render.h>
#include <Core/System/Console.h>
#include <Core/System/Log.h>
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

FRenderServer::FRenderServer()
	: ImGuiDrawDataRing(std::make_unique<FImGuiDrawDataRing>())
	, TextureProxies(std::make_unique<FTextureProxyRegistry>())
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

void FRenderServer::QueueTextureUpload(FTextureCpuSnapshot Snapshot)
{
	std::lock_guard<std::mutex> Lock(PendingUploadMutex);
	PendingTextureUploads.push_back(std::move(Snapshot));
}

void FRenderServer::RequestTextureDestroy(std::string CatalogKey)
{
	std::lock_guard<std::mutex> Lock(PendingUploadMutex);
	PendingTextureDestroys.push_back(std::move(CatalogKey));
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

void FRenderServer::ExecuteFrame(FRenderFramePacket Packet)
{
	CurrentFrameIndex = Packet.FrameIndex;

	if (Packet.bResizeFramebuffer && Packet.FramebufferWidth > 0 && Packet.FramebufferHeight > 0)
	{
		LastFramebufferWidth = Packet.FramebufferWidth;
		LastFramebufferHeight = Packet.FramebufferHeight;
		RHIServer.RequestResize(Packet.FramebufferWidth, Packet.FramebufferHeight);
	}

	std::vector<FTextureCpuSnapshot> Uploads = std::move(Packet.TextureUploads);
	std::vector<std::string> Destroys;
	{
		std::lock_guard<std::mutex> Lock(PendingUploadMutex);
		Uploads.insert(
			Uploads.end(),
			std::make_move_iterator(PendingTextureUploads.begin()),
			std::make_move_iterator(PendingTextureUploads.end()));
		PendingTextureUploads.clear();
		Destroys.swap(PendingTextureDestroys);
	}

	for (std::string& Key : Destroys)
	{
		TextureProxies->Destroy(RHIServer, Key);
	}
	for (FTextureCpuSnapshot& Snap : Uploads)
	{
		TextureProxies->UploadOrUpdate(RHIServer, std::move(Snap));
	}

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
