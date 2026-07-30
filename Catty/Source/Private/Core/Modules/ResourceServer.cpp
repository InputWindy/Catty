#include "ResourceServer.h"

#include <Core/Log.h>

namespace Catty
{

FResourceServer::~FResourceServer()
{
	Shutdown();
}

bool FResourceServer::OnInitialize()
{
	return true;
}

void FResourceServer::OnShutdown()
{
	FThreadedServer::Flush();
	std::lock_guard<std::mutex> Lock(RegistryMutex);
	Registry.clear();
	NextResourceValue = 1;
	CATTY_CORE_INFO("ResourceServer shut down");
}

FResourceId FResourceServer::RequestLoad(std::string Path)
{
	if (Path.empty())
	{
		CATTY_CORE_ERROR("FResourceServer::RequestLoad: empty path");
		return {};
	}

	if (!IsInitialized())
	{
		CATTY_CORE_ERROR("FResourceServer::RequestLoad: server not initialized");
		return {};
	}

	FResourceId Id;
	{
		std::lock_guard<std::mutex> Lock(RegistryMutex);
		Id.Value = NextResourceValue++;
		FResourceRecord Record;
		Record.Path = Path;
		Record.State = EResourceLoadState::Pending;
		Registry.emplace(Id.Value, std::move(Record));
	}

	Enqueue([this, Id, Path = std::move(Path)](FThreadedServer& /*Server*/)
	{
		// Placeholder loader: path identity only until real asset IO exists.
		const bool bSuccess = !Path.empty();
		if (bSuccess)
		{
			CATTY_CORE_INFO("Resource ready: id={} path=\"{}\"", Id.Value, Path);
		}
		else
		{
			CATTY_CORE_ERROR("Resource failed: id={} path=\"{}\"", Id.Value, Path);
		}
		CompleteLoad(Id, bSuccess);
	});

	return Id;
}

void FResourceServer::CompleteLoad(FResourceId Id, bool bSuccess)
{
	{
		std::lock_guard<std::mutex> Lock(RegistryMutex);
		const auto It = Registry.find(Id.Value);
		if (It == Registry.end())
		{
			return;
		}
		It->second.State = bSuccess ? EResourceLoadState::Ready : EResourceLoadState::Failed;
	}
	LoadCv.notify_all();
}

EResourceLoadState FResourceServer::GetLoadState(FResourceId Id) const
{
	if (!Id.IsValid())
	{
		return EResourceLoadState::Invalid;
	}

	std::lock_guard<std::mutex> Lock(RegistryMutex);
	const auto It = Registry.find(Id.Value);
	if (It == Registry.end())
	{
		return EResourceLoadState::Invalid;
	}
	return It->second.State;
}

bool FResourceServer::IsReady(FResourceId Id) const
{
	return GetLoadState(Id) == EResourceLoadState::Ready;
}

bool FResourceServer::TryGetPath(FResourceId Id, std::string& OutPath) const
{
	if (!Id.IsValid())
	{
		return false;
	}

	std::lock_guard<std::mutex> Lock(RegistryMutex);
	const auto It = Registry.find(Id.Value);
	if (It == Registry.end())
	{
		return false;
	}
	OutPath = It->second.Path;
	return true;
}

void FResourceServer::Flush(FResourceId Id)
{
	if (!Id.IsValid())
	{
		return;
	}

	std::unique_lock<std::mutex> Lock(RegistryMutex);
	LoadCv.wait(Lock, [this, Id]()
	{
		const auto It = Registry.find(Id.Value);
		if (It == Registry.end())
		{
			return true;
		}
		return It->second.State != EResourceLoadState::Pending;
	});
}

void FResourceServer::Release(FResourceId Id)
{
	if (!Id.IsValid())
	{
		return;
	}

	{
		std::lock_guard<std::mutex> Lock(RegistryMutex);
		Registry.erase(Id.Value);
	}
	LoadCv.notify_all();
}

bool FResourceServer::HasPendingLoads() const
{
	std::lock_guard<std::mutex> Lock(RegistryMutex);
	for (const auto& Pair : Registry)
	{
		if (Pair.second.State == EResourceLoadState::Pending)
		{
			return true;
		}
	}
	return false;
}

} // namespace Catty
