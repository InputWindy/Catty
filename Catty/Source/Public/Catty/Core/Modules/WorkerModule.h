#pragma once

#include "Catty/Core/Export.h"
#include "Catty/Core/Module.h"
#include "Catty/Core/WorkerPool.h"

#include <vector>

namespace Catty
{

class CATTY_API FWorkerModule final : public IModule
{
public:
	const char* GetName() const override { return "Worker"; }

	void GetDependencies(std::vector<std::string>& OutNames) const override
	{
		OutNames.push_back("Resource");
	}

	bool OnStage(EModuleStage Stage, FApp& App, FStageContext& Ctx) override;

	[[nodiscard]] FWorkerPool& GetWorkerPool() { return WorkerPool; }
	[[nodiscard]] const FWorkerPool& GetWorkerPool() const { return WorkerPool; }

private:
	FWorkerPool WorkerPool;
};

} // namespace Catty
