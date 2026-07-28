#pragma once

#include "Core/Module.h"
#include "CRenderServerApi.h"
#include "Render/RenderServer.h"

#include <vector>

namespace Catty
{

/** Render server + RHI. Plugin id: CRenderServer. */
class CATTY_CRENDERSERVER_MODULE_API FRenderModule final : public IModule
{
public:
	const char* GetName() const override { return "CRenderServer"; }

	void GetDependencies(std::vector<std::string>& OutNames) const override
	{
		OutNames.push_back("CPlatformWindow");
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
