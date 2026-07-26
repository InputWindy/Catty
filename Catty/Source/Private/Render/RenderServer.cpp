#include "Catty/Render/RenderServer.h"

#include "Catty/Core/Log.h"

#include <atomic>

namespace Catty
{

FRenderServer::~FRenderServer()
{
	// Shut down while the derived type is still intact (OnShutdown is virtual).
	Shutdown();
}

bool FRenderServer::OnInitialize()
{
	return true;
}

void FRenderServer::OnShutdown()
{
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
	CATTY_CORE_INFO("RenderServer shut down");
}

bool FRenderServer::InitializeRHI(FPlatformWindow& Window, ERHIBackend Backend)
{
	if (!IsInitialized())
	{
		CATTY_CORE_ERROR("FRenderServer::InitializeRHI: server not initialized");
		return false;
	}

	if (!Window.HasOsWindow())
	{
		CATTY_CORE_INFO("FRenderServer::InitializeRHI: no OS window (headless); skipping RHI");
		return true;
	}

	void* NativeHandle = Window.GetNativeHandle();
	if (!NativeHandle)
	{
		CATTY_CORE_ERROR("FRenderServer::InitializeRHI: native window handle is null");
		return false;
	}

	int Width = 0;
	int Height = 0;
	Window.GetFramebufferSize(Width, Height);
	if (Width <= 0 || Height <= 0)
	{
		CATTY_CORE_ERROR("FRenderServer::InitializeRHI: invalid framebuffer size {}x{}", Width, Height);
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
		CATTY_CORE_ERROR("FRenderServer::InitializeRHI failed");
		return false;
	}

	CATTY_CORE_INFO("FRenderServer RHI ready ({}x{})", Width, Height);
	return true;
}

bool FRenderServer::HasRHI() const
{
	return static_cast<bool>(RHI);
}

void FRenderServer::RequestClearPresent(float R, float G, float B, float A)
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
		RHI->BeginFrame();
		RHI->Clear(R, G, B, A);
		RHI->EndFrame();
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
