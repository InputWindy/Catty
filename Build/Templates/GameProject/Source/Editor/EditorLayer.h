#pragma once

#include <Catty/Core/Layer.h>

/**
 * Project-side editor overlay (ImGui tooling / demo).
 * Push from the game App when building with editor support enabled.
 */
class FEditorLayer final : public Catty::FLayer
{
public:
	FEditorLayer();
	~FEditorLayer() override = default;

	virtual void OnUpdate(float DeltaSeconds) override;

	bool bShowDemoWindow = true;
};
