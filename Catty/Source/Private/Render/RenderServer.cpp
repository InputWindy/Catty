#include "Catty/Render/RenderServer.h"

#include "Catty/Core/Log.h"

#include <cstdint>

#include <vulkan/vulkan.h>

namespace Catty
{

FRenderServer::FRenderServer() = default;

FRenderServer::~FRenderServer()
{
	Shutdown();
}

bool FRenderServer::ProbeVulkan() const
{
	// Link/header smoke check — full VkInstance comes later.
	const std::uint32_t HeaderVersion = VK_HEADER_VERSION;
	CATTY_CORE_INFO("Vulkan headers OK (VK_HEADER_VERSION = {})", HeaderVersion);

#if defined(VK_API_VERSION_1_3)
	std::uint32_t InstanceVersion = 0;
	const VkResult Result = vkEnumerateInstanceVersion(&InstanceVersion);
	if (Result == VK_SUCCESS)
	{
		CATTY_CORE_INFO(
			"Vulkan loader OK (instance version {}.{}.{})",
			VK_VERSION_MAJOR(InstanceVersion),
			VK_VERSION_MINOR(InstanceVersion),
			VK_VERSION_PATCH(InstanceVersion));
		return true;
	}

	CATTY_CORE_ERROR("vkEnumerateInstanceVersion failed ({})", static_cast<int>(Result));
	return false;
#else
	CATTY_CORE_WARN("vkEnumerateInstanceVersion unavailable; skipping loader probe");
	return true;
#endif
}

bool FRenderServer::Initialize()
{
	if (bInitialized)
	{
		return true;
	}

	if (!ProbeVulkan())
	{
		CATTY_CORE_ERROR("FRenderServer::Initialize failed (Vulkan probe)");
		return false;
	}

	if (!RenderThread.Start("CattyRenderThread"))
	{
		CATTY_CORE_ERROR("FRenderServer::Initialize failed (render thread)");
		return false;
	}

	// Smoke: prove game→render enqueue + Flush works.
	RenderThread.Enqueue([]()
	{
		CATTY_CORE_INFO("Render server task executed on render thread");
	});
	RenderThread.Flush();

	bInitialized = true;
	CATTY_CORE_INFO("Render server initialized");
	return true;
}

void FRenderServer::Shutdown()
{
	if (!bInitialized)
	{
		return;
	}

	RenderThread.Flush();
	RenderThread.Stop();
	bInitialized = false;
	CATTY_CORE_INFO("Render server shutdown");
}

void FRenderServer::Enqueue(FRenderTask Task)
{
	RenderThread.Enqueue(std::move(Task));
}

void FRenderServer::Flush()
{
	RenderThread.Flush();
}

} // namespace Catty
