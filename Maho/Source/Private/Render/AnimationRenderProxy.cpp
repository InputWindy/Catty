#include "AnimationRenderProxy.h"

#include <Core/System/Log.h>
#include <Render/RHI/RHIServer.h>

#include <utility>

namespace Maho
{

namespace
{

} // namespace

FAnimationRenderProxy::FAnimationRenderProxy(std::string InCatalogKey)
	: CatalogKey(std::move(InCatalogKey))
{
}

void FAnimationProxyRegistry::BeginUpload(
	FRHIServer& /*RHIServer*/,
	FAnimationCpuSnapshot&& Snapshot,
	FTransferHandle Handle)
{
	if (Snapshot.CatalogKey.empty())
	{
		SetTransferHandleState(Handle, ETransferState::Failed);
		return;
	}

	const std::string Key = Snapshot.CatalogKey;
	FAnimationRenderProxy* Proxy = FindProxy(Key);
	if (!Proxy)
	{
		auto NewProxy = std::make_unique<FAnimationRenderProxy>(Key);
		Proxy = NewProxy.get();
		Proxies.emplace(Key, std::move(NewProxy));
	}

	Proxy->SkeletonCatalogKey = std::move(Snapshot.SkeletonCatalogKey);
	Proxy->DurationSeconds = Snapshot.DurationSeconds;
	Proxy->Tracks = std::move(Snapshot.Tracks);
	Proxy->UploadedGeneration = Snapshot.Generation;
	Proxy->bReady = true;
	SetTransferHandleState(Handle, ETransferState::Succeeded);
}

void FAnimationProxyRegistry::Destroy(
	FRHIServer& /*RHIServer*/,
	const std::string& CatalogKey,
	FTransferHandle Handle)
{
	if (CatalogKey == DefaultKey)
	{
		SetTransferHandleState(Handle, ETransferState::Succeeded);
		return;
	}

	Proxies.erase(CatalogKey);
	SetTransferHandleState(Handle, ETransferState::Succeeded);
}

void FAnimationProxyRegistry::DestroyAll(FRHIServer& /*RHIServer*/)
{
	Proxies.clear();
}

void FAnimationProxyRegistry::EnsureDefaultPlaceholder()
{
	if (FindProxy(DefaultKey))
	{
		return;
	}

	auto Proxy = std::make_unique<FAnimationRenderProxy>(DefaultKey);
	Proxy->DurationSeconds = 0.f;
	Proxy->UploadedGeneration = 1;
	Proxy->bReady = true;
	Proxies.emplace(DefaultKey, std::move(Proxy));
}

FAnimationRenderProxy* FAnimationProxyRegistry::FindProxy(const std::string& CatalogKey) const
{
	const auto It = Proxies.find(CatalogKey);
	return It == Proxies.end() ? nullptr : It->second.get();
}

FAnimationRenderProxy* FAnimationProxyRegistry::FindProxyOrDefault(const std::string& CatalogKey) const
{
	if (FAnimationRenderProxy* Proxy = FindProxy(CatalogKey); Proxy && Proxy->IsReady())
	{
		return Proxy;
	}
	return GetDefaultProxy();
}

FAnimationRenderProxy* FAnimationProxyRegistry::GetDefaultProxy() const
{
	return FindProxy(DefaultKey);
}

} // namespace Maho
