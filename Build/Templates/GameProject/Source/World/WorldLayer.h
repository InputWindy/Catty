#pragma once

#include "World/World.h"

#include <Core/Sequencer/EngineExtension.h>

/**
 * Project layer that owns and ticks FWorld.
 * Push as a normal layer (below editor overlays).
 */
class FWorldLayer final : public Catty::FLayer
{
public:
	explicit FWorldLayer(std::string WorldName = "MainWorld");
	~FWorldLayer() override = default;

	virtual bool ExecuteStage(Catty::EEngineStage Stage) override;

	[[nodiscard]] FWorld& GetWorld() { return World; }
	[[nodiscard]] const FWorld& GetWorld() const { return World; }

private:
	std::string WorldName;
	FWorld World;
	bool bWorldReady = false;
};
