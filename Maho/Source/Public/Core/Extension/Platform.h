#pragma once

#include <Core/Delegate.h>
#include <Core/DependsPack.h>
#include <Core/Export.h>
#include <Core/Sequencer/EngineExtension.h>
#include <Core/System/PlatformWindow.h>
#include <Core/TypeList.h>

#include <cstdint>

namespace Maho
{

class FRenderSystem;

MAHO_DECLARE_MULTICAST_DELEGATE(FOnRequestExit);

/**
 * Built-in platform window / headless clock extension.
 * Sole owner of the main FPlatformWindow. Shutdown after FRenderSystem (TearDown needs the window).
 * BeginFrame: PollEvents / ShouldClose / headless auto-exit (before FRenderSystem ImGui NewFrame).
 * Exit requests Broadcast OnRequestExit (FApp binds in Init).
 */
class MAHO_API FPlatformSystem final
	: public IEngineExtension
	, public TDependsPack<
		TDependsOn<EEngineStage::Shutdown, TTypeList<FRenderSystem>, EExtensionDepStrength::Weak>>
{
public:
	[[nodiscard]] FPlatformWindow* GetWindow() { return PlatformWindow.get(); }
	[[nodiscard]] const FPlatformWindow* GetWindow() const { return PlatformWindow.get(); }

	[[nodiscard]] FOnRequestExit& GetOnRequestExit() { return OnRequestExit; }
	[[nodiscard]] const FOnRequestExit& GetOnRequestExit() const { return OnRequestExit; }

	[[nodiscard]] bool IsHeadlessAutoExit() const { return bAutoExitAfterFrames; }
	[[nodiscard]] std::uint64_t GetAutoExitFrameCount() const { return AutoExitFrameCount; }

private:
	const char* GetName() const override { return "Platform"; }

	bool ExecuteStage(EEngineStage Stage) override;

	FPlatformWindowPtr PlatformWindow;
	FOnRequestExit OnRequestExit;
	FDelegateHandle AppRequestExitHandle;
	bool bAutoExitAfterFrames = false;
	std::uint64_t AutoExitFrameCount = 3;
};

} // namespace Maho
