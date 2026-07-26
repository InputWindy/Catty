#include "Catty/Render/RenderServer.h"

#include "Catty/Core/Log.h"

#include <cstdint>

#include <vulkan/vulkan.h>

namespace Catty
{

FRenderServer::~FRenderServer()
{
	// Shut down while the derived type is still intact (OnShutdown is virtual).
	Shutdown();
}

bool FRenderServer::ProbeVulkan() const
{
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

bool FRenderServer::OnInitialize()
{
	if (!ProbeVulkan())
	{
		return false;
	}

	Enqueue([](FThreadedServer& /*Server*/)
	{
		CATTY_CORE_INFO("Render server task executed on render thread");
	});
	Flush();
	return true;
}

void FRenderServer::OnShutdown()
{
}

} // namespace Catty
