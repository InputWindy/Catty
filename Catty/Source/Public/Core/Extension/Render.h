#pragma once

/**
 * Render extension: owns FRenderServer.
 * Games hook PreRender / PostRender on Layers.
 */

#include <Core/DependsPack.h>
#include <Core/Export.h>
#include <Core/Sequencer/EngineExtension.h>
#include <Core/Extension/Platform.h>
#include <Core/TypeList.h>
#include <Render/RenderServer.h>

namespace Catty
{

/**
 * RenderCore extension. Init after FPlatformSystem (needs live window).
 * BeginFrame: ImGui NewFrame. Render: Present / submit.
 */
class CATTY_API FRenderSystem final
	: public IEngineExtension
	, public TDependsPack<
		TDependsOn<EEngineStage::Init, TTypeList<FPlatformSystem>>,
		TDependsOn<EEngineStage::BeginFrame, TTypeList<FPlatformSystem>>>
{
public:
	[[nodiscard]] FRenderServer& GetRenderServer() { return RenderServer; }
	[[nodiscard]] const FRenderServer& GetRenderServer() const { return RenderServer; }

	const char* GetName() const override { return "Render"; }
	bool ExecuteStage(EEngineStage Stage) override;

private:
	FRenderServer RenderServer;
};

} // namespace Catty
