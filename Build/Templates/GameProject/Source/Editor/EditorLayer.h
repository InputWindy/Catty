#pragma once

#include <Core/Layer.h>
#include <Core/Module.h>

/**
 * Project-side editor overlay (ImGui tooling / demo).
 * Push from the game App when building with editor support enabled.
 */
class FEditorLayer final : public Catty::FLayer
{
public:
	FEditorLayer();
	~FEditorLayer() override = default;

	virtual void OnUpdate(
		Catty::EModuleStage Stage,
		Catty::FApp& App,
		Catty::FStageContext& Ctx) override;

	bool bShowDemoWindow = true;
};
