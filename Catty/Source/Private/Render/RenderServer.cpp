#include <Render/RenderServer.h>

#include <Core/System/ConsoleManager.h>
#include <Core/System/Log.h>
#include "Render/RHI/VulkanRHI.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <vector>

#include <imgui.h>
#include <imgui_impl_vulkan.h>

namespace Catty
{

namespace
{

static TAutoConsoleVariable GCVarMaxFramesInFlight(
	"r.MaxFramesInFlight",
	3,
	"Max Game frames submitted to RenderCore before waiting (1..3)");

struct FImGuiFrameSlot
{
	ImDrawData DrawData;
	bool bOccupied = false;
	std::uint64_t FrameIndex = 0;

	void ReleaseOwnedLists()
	{
		for (ImDrawList* List : DrawData.CmdLists)
		{
			if (List)
			{
				IM_DELETE(List);
			}
		}
		DrawData.Clear();
		bOccupied = false;
		FrameIndex = 0;
	}
};

} // namespace

struct FImGuiDrawDataRing
{
	std::array<FImGuiFrameSlot, FRenderServer::MaxFramesInFlightCap> Slots;

	~FImGuiDrawDataRing()
	{
		for (FImGuiFrameSlot& Slot : Slots)
		{
			Slot.ReleaseOwnedLists();
		}
	}

	void ReleaseAll()
	{
		for (FImGuiFrameSlot& Slot : Slots)
		{
			Slot.ReleaseOwnedLists();
		}
	}

	void ReleaseFrame(std::uint64_t FrameIndex)
	{
		const int SlotIndex = static_cast<int>(FrameIndex % FRenderServer::MaxFramesInFlightCap);
		FImGuiFrameSlot& Slot = Slots[static_cast<std::size_t>(SlotIndex)];
		if (Slot.bOccupied && Slot.FrameIndex == FrameIndex)
		{
			Slot.ReleaseOwnedLists();
		}
	}

	[[nodiscard]] int CaptureFromImGui(std::uint64_t FrameIndex)
	{
		ImDrawData* Source = ImGui::GetDrawData();
		if (!Source || !Source->Valid || Source->CmdListsCount <= 0)
		{
			return -1;
		}

		const int SlotIndex = static_cast<int>(FrameIndex % FRenderServer::MaxFramesInFlightCap);
		FImGuiFrameSlot& Slot = Slots[static_cast<std::size_t>(SlotIndex)];
		// Free previous occupant on the game thread only (CloneOutput shares ImDrawListSharedData).
		Slot.ReleaseOwnedLists();

		Slot.DrawData.Valid = true;
		Slot.DrawData.DisplayPos = Source->DisplayPos;
		Slot.DrawData.DisplaySize = Source->DisplaySize;
		Slot.DrawData.FramebufferScale = Source->FramebufferScale;
		Slot.DrawData.OwnerViewport = Source->OwnerViewport;

		for (int ListIndex = 0; ListIndex < Source->CmdListsCount; ++ListIndex)
		{
			ImDrawList* Clone = Source->CmdLists[ListIndex]->CloneOutput();
			if (!Clone)
			{
				continue;
			}
			// CloneOutput copies buffers but leaves write pointers unset; AddDrawList asserts on them.
			Clone->_VtxCurrentIdx = static_cast<unsigned int>(Clone->VtxBuffer.Size);
			Clone->_VtxWritePtr = Clone->VtxBuffer.Data + Clone->VtxBuffer.Size;
			Clone->_IdxWritePtr = Clone->IdxBuffer.Data + Clone->IdxBuffer.Size;
			Slot.DrawData.AddDrawList(Clone);
		}

		if (Slot.DrawData.CmdListsCount <= 0)
		{
			Slot.ReleaseOwnedLists();
			return -1;
		}

		Slot.FrameIndex = FrameIndex;
		Slot.bOccupied = true;
		return SlotIndex;
	}
};

FRenderServer::FRenderServer()
	: ImGuiDrawDataRing(std::make_unique<FImGuiDrawDataRing>())
{
}

FRenderServer::~FRenderServer()
{
	TearDown();
}

bool FRenderServer::OnInitialize()
{
	return true;
}

void FRenderServer::OnShutdown()
{
	bImGuiEnabled = false;
	if (RHI)
	{
		Enqueue([this](FThreadedServer& /*Server*/)
		{
			if (RHI)
			{
				RHI->Shutdown();
				RHI.reset();
			}
		});
		Flush();
	}
	CATTY_CORE_INFO("RenderServer: render worker shut down");
}

int FRenderServer::GetMaxFramesInFlight() const
{
	const int Requested = GCVarMaxFramesInFlight.GetValue();
	return (std::clamp)(Requested, 1, MaxFramesInFlightCap);
}

void FRenderServer::WaitForRenderFrame(std::uint64_t FrameIndex)
{
	std::unique_lock<std::mutex> Lock(FenceMutex);
	FenceCv.wait(Lock, [this, FrameIndex]()
	{
		return LastCompletedRenderFrame >= FrameIndex;
	});
}

void FRenderServer::SignalRenderFrameComplete(std::uint64_t FrameIndex)
{
	{
		std::lock_guard<std::mutex> Lock(FenceMutex);
		if (FrameIndex > LastCompletedRenderFrame)
		{
			LastCompletedRenderFrame = FrameIndex;
		}
	}
	FenceCv.notify_all();
}

void FRenderServer::SetClearColor(float R, float G, float B, float A)
{
	ClearColorR = R;
	ClearColorG = G;
	ClearColorB = B;
	ClearColorA = A;
}

bool FRenderServer::Boot(FPlatformWindow& InWindow, const FEngineConfig& Config)
{
	BoundWindow = &InWindow;
	ClearColorR = Config.ClearColorR;
	ClearColorG = Config.ClearColorG;
	ClearColorB = Config.ClearColorB;
	ClearColorA = Config.ClearColorA;

	if (!Initialize())
	{
		CATTY_CORE_ERROR("RenderServer: ThreadedServer Initialize failed");
		BoundWindow = nullptr;
		return false;
	}

	if (!InitializeRHI(InWindow))
	{
		CATTY_CORE_ERROR("RenderServer: InitializeRHI failed");
		Shutdown();
		BoundWindow = nullptr;
		return false;
	}

	if (InWindow.HasOsWindow())
	{
		InWindow.GetFramebufferSize(LastFramebufferWidth, LastFramebufferHeight);
		if (!ImGui.Initialize(InWindow, *this, Config.ProjectConfigDir))
		{
			CATTY_CORE_ERROR("RenderServer: ImGui Initialize failed");
			Shutdown();
			BoundWindow = nullptr;
			return false;
		}
		SetImGuiEnabled(true);
	}

	{
		std::lock_guard<std::mutex> Lock(FenceMutex);
		LastCompletedRenderFrame = 0;
	}
	CurrentFrameIndex = 0;
	PendingImGuiSlotIndex = -1;

	CATTY_CORE_INFO("RenderServer: Boot ok (MaxFramesInFlight={})", GetMaxFramesInFlight());
	return true;
}

void FRenderServer::TearDown()
{
	if (IsInitialized())
	{
		Flush();
	}

	if (ImGui.IsInitialized())
	{
		ImGui.Shutdown(*this);
	}
	if (IsInitialized())
	{
		Shutdown();
	}
	if (ImGuiDrawDataRing)
	{
		ImGuiDrawDataRing->ReleaseAll();
	}

	BoundWindow = nullptr;
	LastFramebufferWidth = 0;
	LastFramebufferHeight = 0;
	PendingImGuiSlotIndex = -1;
	CurrentFrameIndex = 0;
	{
		std::lock_guard<std::mutex> Lock(FenceMutex);
		LastCompletedRenderFrame = 0;
	}
}

void FRenderServer::WaitBeforeImGuiNewFrame(std::uint64_t FrameIndex)
{
	// Serialize ImGui: never NewFrame while the previous frame's RenderDrawData may still run.
	if (FrameIndex > 1)
	{
		WaitForRenderFrame(FrameIndex - 1);
	}
}

void FRenderServer::Render(std::uint64_t FrameIndex)
{
	CurrentFrameIndex = FrameIndex;
	PendingImGuiSlotIndex = -1;

	const int MaxInFlight = GetMaxFramesInFlight();
	if (FrameIndex > static_cast<std::uint64_t>(MaxInFlight))
	{
		WaitForRenderFrame(FrameIndex - static_cast<std::uint64_t>(MaxInFlight));
	}

	SyncFramebufferSize();

	if (ImGui.IsInitialized())
	{
		ImGui.EndFrame();
		if (ImGuiDrawDataRing)
		{
			PendingImGuiSlotIndex = ImGuiDrawDataRing->CaptureFromImGui(CurrentFrameIndex);
		}
	}

	SubmitBeginMainPass(ClearColorR, ClearColorG, ClearColorB, ClearColorA);
	SubmitRenderUI(PendingImGuiSlotIndex);
	SubmitEndFrameAndFence(CurrentFrameIndex);
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

bool FRenderServer::InitializeRHI(FPlatformWindow& InWindow, ERHIBackend Backend)
{
	if (!IsInitialized())
	{
		CATTY_CORE_ERROR("RenderServer::InitializeRHI: server not initialized");
		return false;
	}

	if (!InWindow.HasOsWindow())
	{
		CATTY_CORE_INFO("RenderServer::InitializeRHI: headless; skipping RHI");
		return true;
	}

	void* NativeHandle = InWindow.GetNativeHandle();
	if (!NativeHandle)
	{
		CATTY_CORE_ERROR("RenderServer::InitializeRHI: native window handle is null");
		return false;
	}

	int Width = 0;
	int Height = 0;
	InWindow.GetFramebufferSize(Width, Height);
	if (Width <= 0 || Height <= 0)
	{
		CATTY_CORE_ERROR("RenderServer::InitializeRHI: invalid framebuffer {}x{}", Width, Height);
		return false;
	}

	std::atomic<bool> bOk{false};
	Enqueue([this, Backend, NativeHandle, Width, Height, &bOk](FThreadedServer& /*Server*/)
	{
		RHI = FRHIFactory::Create(Backend);
		if (!RHI)
		{
			bOk.store(false);
			return;
		}

		FRHIInitDesc Desc;
		Desc.Backend = Backend;
		Desc.NativeWindowHandle = NativeHandle;
		Desc.FramebufferWidth = Width;
		Desc.FramebufferHeight = Height;

		const bool bInitialized = RHI->Initialize(Desc);
		if (!bInitialized)
		{
			RHI.reset();
		}
		bOk.store(bInitialized);
	});
	Flush();

	if (!bOk.load())
	{
		CATTY_CORE_ERROR("RenderServer::InitializeRHI failed");
		return false;
	}

	CATTY_CORE_INFO("RenderServer RHI ready ({}x{})", Width, Height);
	return true;
}

FVulkanRHI* FRenderServer::GetVulkanRHI() const
{
	return dynamic_cast<FVulkanRHI*>(RHI.get());
}

void FRenderServer::SubmitBeginMainPass(float R, float G, float B, float A)
{
	if (!RHI)
	{
		return;
	}

	Enqueue([this, R, G, B, A](FThreadedServer& /*Server*/)
	{
		if (!RHI)
		{
			return;
		}

		FVulkanRHI* VulkanRHI = dynamic_cast<FVulkanRHI*>(RHI.get());
		if (!VulkanRHI)
		{
			RHI->BeginFrame();
			RHI->Clear(R, G, B, A);
			return;
		}

		VulkanRHI->BeginFrame();
		VulkanRHI->BeginMainPass(R, G, B, A);
	});
}

void FRenderServer::SubmitRenderUI(int SlotIndex)
{
	if (!RHI || !bImGuiEnabled || SlotIndex < 0 || !ImGuiDrawDataRing)
	{
		return;
	}

	Enqueue([this, SlotIndex](FThreadedServer& /*Server*/)
	{
		FVulkanRHI* VulkanRHI = dynamic_cast<FVulkanRHI*>(RHI.get());
		if (!VulkanRHI || !ImGuiDrawDataRing)
		{
			return;
		}

		FImGuiFrameSlot& Slot = ImGuiDrawDataRing->Slots[static_cast<std::size_t>(SlotIndex)];
		if (!Slot.bOccupied || !Slot.DrawData.Valid || Slot.DrawData.CmdListsCount <= 0)
		{
			return;
		}

		ImGui_ImplVulkan_RenderDrawData(&Slot.DrawData, VulkanRHI->GetVkCommandBuffer());
	});
}

void FRenderServer::SubmitEndFrameAndFence(std::uint64_t FrameIndex)
{
	if (!IsInitialized())
	{
		SignalRenderFrameComplete(FrameIndex);
		return;
	}

	Enqueue([this, FrameIndex](FThreadedServer& /*Server*/)
	{
		if (RHI)
		{
			FVulkanRHI* VulkanRHI = dynamic_cast<FVulkanRHI*>(RHI.get());
			if (!VulkanRHI)
			{
				RHI->EndFrame();
			}
			else
			{
				VulkanRHI->EndMainPass();
				VulkanRHI->EndFrame();
			}
		}

		// Do not IM_DELETE ImGui clones here — shared with ImDrawListSharedData; free on game thread.
		SignalRenderFrameComplete(FrameIndex);
	});
}

void FRenderServer::RequestResize(int Width, int Height)
{
	if (!RHI || Width <= 0 || Height <= 0)
	{
		return;
	}

	Enqueue([this, Width, Height](FThreadedServer& /*Server*/)
	{
		if (RHI)
		{
			RHI->Resize(Width, Height);
		}
	});
}

} // namespace Catty
