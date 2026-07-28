#pragma once

#include "Catty/Core/Export.h"
#include "Catty/Core/Module.h"
#include "Catty/Render/RenderServer.h"

#include <vector>

namespace Catty
{

/** Render server + RHI. Depends on Platform. */
class CATTY_API FRenderModule final : public IModule
{
public:
	const char* GetName() const override { return "Render"; }

	void GetDependencies(std::vector<std::string>& OutNames) const override
	{
		OutNames.push_back("Platform");
	}

	bool OnStage(EModuleStage Stage, FApp& App, FStageContext& Ctx) override;

	[[nodiscard]] FRenderServer& GetServer() { return RenderServer; }
	[[nodiscard]] const FRenderServer& GetServer() const { return RenderServer; }

private:
	void SyncFramebufferSize(FApp& App);
	void Flush();

	FRenderServer RenderServer;
	int LastFramebufferWidth = 0;
	int LastFramebufferHeight = 0;
};

} // namespace Catty
