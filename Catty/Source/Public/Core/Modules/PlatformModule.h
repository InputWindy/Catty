#pragma once

#include <Core/Export.h>
#include <Core/Module.h>
#include <Core/PlatformWindow.h>

#include <cstdint>
#include <vector>

namespace Catty
{

/** Built-in platform window / headless clock module (always-on in Catty.dll). */
class CATTY_API FPlatformModule final : public IModule
{
public:
	const char* GetName() const override { return "Platform"; }

	void GetDependencies(EModuleStage /*Stage*/, std::vector<std::string>& /*OutNames*/) const override
	{
	}

	bool ExecuteStage(EModuleStage Stage, FApp& App, FStageContext& Ctx) override;

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
