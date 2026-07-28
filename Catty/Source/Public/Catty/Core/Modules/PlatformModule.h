#pragma once

#include "Catty/Core/Export.h"
#include "Catty/Core/Module.h"
#include "Catty/Platform/PlatformWindow.h"

#include <cstdint>
#include <vector>

namespace Catty
{

/** Platform window / headless clock. Depends on Engine. */
class CATTY_API FPlatformModule final : public IModule
{
public:
	const char* GetName() const override { return "Platform"; }

	void GetDependencies(std::vector<std::string>& OutNames) const override
	{
		OutNames.push_back("Engine");
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
