#pragma once

#include "Core/Module.h"
#include "CPlatformWindowApi.h"
#include "Platform/PlatformWindow.h"

#include <cstdint>
#include <vector>

namespace Catty
{

/** Platform window / headless clock. Plugin id: CPlatformWindow. */
class CATTY_CPLATFORMWINDOW_MODULE_API FPlatformModule final : public IModule
{
public:
	const char* GetName() const override { return "CPlatformWindow"; }

	void GetDependencies(std::vector<std::string>& /*OutNames*/) const override
	{
	}

	bool OnStage(EModuleStage Stage, FApp& App, FStageContext& Ctx) override;

	[[nodiscard]] FPlatformWindow* GetWindow() { return PlatformWindow.get(); }
	[[nodiscard]] const FPlatformWindow* GetWindow() const { return PlatformWindow.get(); }

	[[nodiscard]] bool IsHeadlessAutoExit() const { return bAutoExitAfterFrames; }
	[[nodiscard]] std::uint64_t GetAutoExitFrameCount() const { return AutoExitFrameCount; }

private:
	FPlatformWindowPtr PlatformWindow;
	bool bAutoExitAfterFrames = false;
	std::uint64_t AutoExitFrameCount = 3;
};

} // namespace Catty
