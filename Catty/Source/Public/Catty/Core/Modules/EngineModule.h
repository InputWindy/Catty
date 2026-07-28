#pragma once

#include "Catty/Core/Engine.h"
#include "Catty/Core/Export.h"
#include "Catty/Core/Module.h"

namespace Catty
{

/** Owns FEngine bookkeeping (config store + frame index). */
class CATTY_API FEngineModule final : public IModule
{
public:
	const char* GetName() const override { return "Engine"; }

	bool OnStage(EModuleStage Stage, FApp& App, FStageContext& Ctx) override;

	[[nodiscard]] FEngine& GetEngine() { return Engine; }
	[[nodiscard]] const FEngine& GetEngine() const { return Engine; }

private:
	FEngine Engine;
};

} // namespace Catty
