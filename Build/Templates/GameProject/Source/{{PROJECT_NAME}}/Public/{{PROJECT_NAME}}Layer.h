#pragma once

#include <Core/Sequencer/EngineExtension.h>

#include <string>

/**
 * Default game Layer created with the project.
 * Hand-written — do not put RegisterExtension lists here (those live in Source/Generated/*App.cpp).
 */
class F{{PROJECT_NAME}}Layer final : public Catty::FLayer
{
public:
	explicit F{{PROJECT_NAME}}Layer(std::string InName = "{{PROJECT_NAME}}")
		: Catty::FLayer(std::move(InName))
	{
	}

	bool ExecuteStage(Catty::EEngineStage Stage) override;
};
