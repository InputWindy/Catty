#pragma once

#include <Core/Export.h>

#include <cstdint>
#include <memory>

namespace Catty
{

enum class ERHIBackend : std::uint8_t
{
	Vulkan = 0,
};

struct FRHIInitDesc
{
	ERHIBackend Backend = ERHIBackend::Vulkan;

	/** Native window handle for WSI (HWND on Win32). */
	void* NativeWindowHandle = nullptr;

	int FramebufferWidth = 0;
	int FramebufferHeight = 0;
};

/**
 * Minimal render-hardware interface (Vulkan-backed implementation today).
 * Created through FRHIFactory; no third-party types in the public API.
 *
 * Example:
 * ```
 *   Catty::FRHIInitDesc Desc;
 *   Desc.Backend = Catty::ERHIBackend::Vulkan;
 *   Desc.NativeWindowHandle = Window->GetNativeHandle();
 *   Window->GetFramebufferSize(Desc.FramebufferWidth, Desc.FramebufferHeight);
 *
 *   Catty::FRHIPtr RHI = Catty::FRHIFactory::Create(Desc.Backend);
 *   RHI->Initialize(Desc);
 *   RHI->BeginFrame();
 *   RHI->Clear(0.1f, 0.1f, 0.15f, 1.0f);
 *   RHI->EndFrame();
 *   RHI->Shutdown();
 * ```
 */
class CATTY_API IRHI
{
public:
	virtual ~IRHI() = default;

	IRHI(const IRHI&) = delete;
	IRHI& operator=(const IRHI&) = delete;

	virtual bool Initialize(const FRHIInitDesc& Desc) = 0;
	virtual void Shutdown() = 0;

	virtual void BeginFrame() = 0;
	virtual void Clear(float R, float G, float B, float A) = 0;
	virtual void EndFrame() = 0;

	virtual void Resize(int Width, int Height) = 0;

	[[nodiscard]] virtual bool IsInitialized() const = 0;

protected:
	IRHI() = default;
};

/** Deleter that frees the object inside Catty.dll (safe across EXE/DLL heaps). */
struct CATTY_API FRHIDeleter
{
	void operator()(IRHI* RHI) const;
};

using FRHIPtr = std::unique_ptr<IRHI, FRHIDeleter>;

/** Creates the RHI backend selected by FRHIInitDesc::Backend. */
class CATTY_API FRHIFactory
{
public:
	FRHIFactory() = delete;

	[[nodiscard]] static FRHIPtr Create(ERHIBackend Backend);
};

} // namespace Catty
