#pragma once

#include "World/World.h"

#include <Core/Layer.h>
#include <Core/Module.h>

/**
 * Project layer that owns and ticks FWorld.
 * Push as a normal layer (below editor overlays).
 */
class FWorldLayer final : public Catty::FLayer
{
public:
	explicit FWorldLayer(std::string WorldName = "MainWorld");
	~FWorldLayer() override = default;

	virtual void OnAttach() override;
	virtual void OnDetach() override;
	virtual void OnUpdate(
		Catty::EModuleStage Stage,
		Catty::FApp& App,
		Catty::FStageContext& Ctx) override;

	[[nodiscard]] FWorld& GetWorld() { return World; }
	[[nodiscard]] const FWorld& GetWorld() const { return World; }

private:
	std::string WorldName;
	FWorld World;
};
