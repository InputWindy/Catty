#include <Render/RenderServer.h>

#include <Core/System/Console.h>
#include <Core/System/Log.h>
#include "Render/UI/ImGuiDrawDataRing.h"

#include <algorithm>

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

FRenderServer::FRenderServer()
	: ImGuiDrawDataRing(std::make_unique<FImGuiDrawDataRing>())
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

bool FRenderServer::Boot(FPlatformWindow& InWindow, const FConfig& Config)
{
	BoundWindow = &InWindow;
	ClearColorR = Config.ClearColorR;
	ClearColorG = Config.ClearColorG;
	ClearColorB = Config.ClearColorB;
	ClearColorA = Config.ClearColorA;

	if (!RHIServer.Initialize())
	{
		MAHO_CORE_ERROR("RenderServer: RHIServer Initialize failed");
		BoundWindow = nullptr;
		return false;
	}

	if (!RHIServer.InitializeRHI(InWindow))
	{
		MAHO_CORE_ERROR("RenderServer: InitializeRHI failed");
		RHIServer.Shutdown();
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
			BoundWindow = nullptr;
			return false;
		}
		SetImGuiEnabled(true);
	}

	RHIServer.ResetFrameFence();
	CurrentFrameIndex = 0;
	PendingImGuiSlotIndex = -1;

	MAHO_CORE_INFO("RenderServer: Boot ok (MaxFramesInFlight={})", GetMaxFramesInFlight());
	return true;
}

void FRenderServer::TearDown()
{
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
	BoundWindow = nullptr;
	LastFramebufferWidth = 0;
	LastFramebufferHeight = 0;
	PendingImGuiSlotIndex = -1;
	CurrentFrameIndex = 0;
}

void FRenderServer::WaitBeforeImGuiNewFrame(std::uint64_t FrameIndex)
{
	// Serialize ImGui: never NewFrame while the previous frame's RenderDrawData may still run.
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

void FRenderServer::ExecuteRenderStages()
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
			if (ImGui.IsInitialized())
			{
				ImGui.EndFrame();
				if (ImGuiDrawDataRing)
				{
					PendingImGuiSlotIndex = ImGuiDrawDataRing->CaptureFromImGui(CurrentFrameIndex);
				}
			}

			RHIServer.SubmitBeginMainPass(ClearColorR, ClearColorG, ClearColorB, ClearColorA);
			if (bImGuiEnabled && PendingImGuiSlotIndex >= 0 && ImGuiDrawDataRing)
			{
				RHIServer.SubmitRenderUI(*ImGuiDrawDataRing, PendingImGuiSlotIndex);
			}
			RHIServer.SubmitEndFrameAndFence(CurrentFrameIndex);

			if (ImGui.IsInitialized())
			{
				// Drain RHI so secondary viewport Vulkan create/render can run on Game safely.
				RHIServer.Flush();
				ImGui.UpdateAndRenderPlatformWindows();
			}
		}
	}
}

void FRenderServer::Render(std::uint64_t FrameIndex)
{
	CurrentFrameIndex = FrameIndex;
	PendingImGuiSlotIndex = -1;

	const int MaxInFlight = GetMaxFramesInFlight();
	if (FrameIndex > static_cast<std::uint64_t>(MaxInFlight))
	{
		RHIServer.WaitForRenderFrame(FrameIndex - static_cast<std::uint64_t>(MaxInFlight));
	}

	SyncFramebufferSize();
	ExecuteRenderStages();
}

void FRenderServer::SyncFramebufferSize()
{
	if (!BoundWindow || !BoundWindow->HasOsWindow() || !HasRHI())
	{
		return;
	}

	int Width = 0;
	int Height = 0;
	BoundWindow->GetFramebufferSize(Width, Height);
	if (Width <= 0 || Height <= 0)
	{
		return;
	}

	if (Width != LastFramebufferWidth || Height != LastFramebufferHeight)
	{
		LastFramebufferWidth = Width;
		LastFramebufferHeight = Height;
		RequestResize(Width, Height);
	}
}

} // namespace Maho
