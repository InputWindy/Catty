#include "Platform/GlfwPlatformWindow.h"

#include "Catty/Core/Log.h"

#include <GLFW/glfw3.h>

#if defined(_WIN32)
#	define GLFW_EXPOSE_NATIVE_WIN32
#	include <GLFW/glfw3native.h>
#endif

namespace Catty
{

namespace
{

bool GbGlfwRuntimeInitialized = false;

void GlfwErrorCallback(int ErrorCode, const char* Description)
{
	CATTY_CORE_ERROR("GLFW error {}: {}", ErrorCode, Description ? Description : "(null)");
}

} // namespace

bool FGlfwPlatformWindow::EnsureRuntimeInitialized()
{
	if (GbGlfwRuntimeInitialized)
	{
		return true;
	}

	glfwSetErrorCallback(GlfwErrorCallback);

	if (glfwInit() != GLFW_TRUE)
	{
		CATTY_CORE_ERROR("FGlfwPlatformWindow: glfwInit failed");
		return false;
	}

	GbGlfwRuntimeInitialized = true;
	CATTY_CORE_INFO("Platform runtime initialized (GLFW {})", glfwGetVersionString());
	return true;
}

void FGlfwPlatformWindow::ShutdownRuntime()
{
	if (!GbGlfwRuntimeInitialized)
	{
		return;
	}

	glfwTerminate();
	GbGlfwRuntimeInitialized = false;
	CATTY_CORE_INFO("Platform runtime shut down");
}

bool FGlfwPlatformWindow::IsRuntimeInitialized()
{
	return GbGlfwRuntimeInitialized;
}

FGlfwPlatformWindow::~FGlfwPlatformWindow()
{
	Destroy();
}

bool FGlfwPlatformWindow::Initialize(const FPlatformWindowDesc& Desc)
{
	if (bRuntimeOwned || Handle)
	{
		CATTY_CORE_ERROR("FGlfwPlatformWindow::Initialize: already initialized");
		return false;
	}

	if (!EnsureRuntimeInitialized())
	{
		return false;
	}

	bRuntimeOwned = true;

	if (Desc.bHeadless)
	{
		CATTY_CORE_INFO("FGlfwPlatformWindow created (headless)");
		return true;
	}

	if (Desc.Width <= 0 || Desc.Height <= 0)
	{
		CATTY_CORE_ERROR("FGlfwPlatformWindow::Initialize: invalid size {}x{}", Desc.Width, Desc.Height);
		return false;
	}

	glfwDefaultWindowHints();
	// No OpenGL/Vulkan context here — surface comes later via Vulkan WSI.
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, Desc.bResizable ? GLFW_TRUE : GLFW_FALSE);

	Handle = glfwCreateWindow(
		Desc.Width,
		Desc.Height,
		Desc.Title.c_str(),
		nullptr,
		nullptr);

	if (!Handle)
	{
		CATTY_CORE_ERROR("FGlfwPlatformWindow::Initialize: glfwCreateWindow failed");
		return false;
	}

	CATTY_CORE_INFO("FGlfwPlatformWindow created: \"{}\" {}x{}", Desc.Title, Desc.Width, Desc.Height);
	return true;
}

void FGlfwPlatformWindow::Destroy()
{
	if (Handle)
	{
		glfwDestroyWindow(Handle);
		Handle = nullptr;
		CATTY_CORE_INFO("FGlfwPlatformWindow OS window destroyed");
	}

	bRuntimeOwned = false;
}

bool FGlfwPlatformWindow::IsValid() const
{
	return bRuntimeOwned || Handle != nullptr;
}

bool FGlfwPlatformWindow::HasOsWindow() const
{
	return Handle != nullptr;
}

bool FGlfwPlatformWindow::ShouldClose() const
{
	return Handle != nullptr && glfwWindowShouldClose(Handle) == GLFW_TRUE;
}

void FGlfwPlatformWindow::SetTitle(const std::string& Title)
{
	if (Handle)
	{
		glfwSetWindowTitle(Handle, Title.c_str());
	}
}

void FGlfwPlatformWindow::GetFramebufferSize(int& OutWidth, int& OutHeight) const
{
	OutWidth = 0;
	OutHeight = 0;
	if (Handle)
	{
		glfwGetFramebufferSize(Handle, &OutWidth, &OutHeight);
	}
}

void* FGlfwPlatformWindow::GetNativeHandle() const
{
#if defined(_WIN32)
	return Handle ? static_cast<void*>(glfwGetWin32Window(Handle)) : nullptr;
#else
	return nullptr;
#endif
}

void* FGlfwPlatformWindow::GetToolkitWindowHandle() const
{
	return Handle;
}

void FGlfwPlatformWindow::PollEvents()
{
	if (GbGlfwRuntimeInitialized)
	{
		glfwPollEvents();
	}
}

double FGlfwPlatformWindow::GetTimeSeconds() const
{
	return GbGlfwRuntimeInitialized ? glfwGetTime() : 0.0;
}

namespace
{

int ToGlfwKey(EKey Key)
{
	switch (Key)
	{
	case EKey::Escape: return GLFW_KEY_ESCAPE;
	case EKey::Space: return GLFW_KEY_SPACE;
	case EKey::Enter: return GLFW_KEY_ENTER;
	case EKey::W: return GLFW_KEY_W;
	case EKey::A: return GLFW_KEY_A;
	case EKey::S: return GLFW_KEY_S;
	case EKey::D: return GLFW_KEY_D;
	case EKey::Q: return GLFW_KEY_Q;
	case EKey::E: return GLFW_KEY_E;
	case EKey::Left: return GLFW_KEY_LEFT;
	case EKey::Right: return GLFW_KEY_RIGHT;
	case EKey::Up: return GLFW_KEY_UP;
	case EKey::Down: return GLFW_KEY_DOWN;
	default: return GLFW_KEY_UNKNOWN;
	}
}

int ToGlfwMouseButton(EMouseButton Button)
{
	switch (Button)
	{
	case EMouseButton::Left: return GLFW_MOUSE_BUTTON_LEFT;
	case EMouseButton::Right: return GLFW_MOUSE_BUTTON_RIGHT;
	case EMouseButton::Middle: return GLFW_MOUSE_BUTTON_MIDDLE;
	default: return GLFW_MOUSE_BUTTON_LEFT;
	}
}

} // namespace

bool FGlfwPlatformWindow::IsKeyDown(EKey Key) const
{
	if (!Handle)
	{
		return false;
	}
	const int GlfwKey = ToGlfwKey(Key);
	return GlfwKey != GLFW_KEY_UNKNOWN && glfwGetKey(Handle, GlfwKey) == GLFW_PRESS;
}

bool FGlfwPlatformWindow::IsMouseButtonDown(EMouseButton Button) const
{
	if (!Handle)
	{
		return false;
	}
	return glfwGetMouseButton(Handle, ToGlfwMouseButton(Button)) == GLFW_PRESS;
}

void FGlfwPlatformWindow::GetCursorPosition(float& OutX, float& OutY) const
{
	OutX = 0.0f;
	OutY = 0.0f;
	if (Handle)
	{
		double X = 0.0;
		double Y = 0.0;
		glfwGetCursorPos(Handle, &X, &Y);
		OutX = static_cast<float>(X);
		OutY = static_cast<float>(Y);
	}
}

void FPlatformWindowDeleter::operator()(FPlatformWindow* Window) const
{
	delete Window;
}

FPlatformWindowPtr FPlatformWindowFactory::Create(const FPlatformWindowDesc& Desc)
{
	switch (Desc.Platform)
	{
	case EPlatform::Glfw:
	{
		auto* Window = new FGlfwPlatformWindow();
		if (!Window->Initialize(Desc))
		{
			delete Window;
			return nullptr;
		}
		return FPlatformWindowPtr{Window};
	}
	}

	CATTY_CORE_ERROR("FPlatformWindowFactory::Create: unsupported EPlatform ({})", static_cast<std::uint32_t>(Desc.Platform));
	return nullptr;
}

void FPlatformWindowFactory::Shutdown()
{
	FGlfwPlatformWindow::ShutdownRuntime();
}

} // namespace Catty
