#include "ResourceServer.h"

#include <Core/System/Log.h>
#include <Core/System/Utf8Path.h>

#include <filesystem>
#include <fstream>

namespace Maho
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
	MAHO_CORE_INFO("ResourceServer shut down");
}

FResourceId FResourceServer::RequestLoad(std::string Path)
{
	if (Path.empty())
	{
		MAHO_CORE_ERROR("FResourceServer::RequestLoad: empty path");
		return {};
	}

	if (!IsInitialized())
	{
		MAHO_CORE_ERROR("FResourceServer::RequestLoad: server not initialized");
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
		std::vector<std::uint8_t> Bytes;
		bool bSuccess = false;

		namespace fs = std::filesystem;
		std::error_code ErrorCode;
		const fs::path FilePath = PathFromUtf8(Path);
		if (!fs::is_regular_file(FilePath, ErrorCode) || ErrorCode)
		{
			MAHO_CORE_ERROR(
				"Resource BulkData failed: id={} path=\"{}\" (not a regular file)",
				Id.Value,
				Path);
		}
		else
		{
			std::ifstream File(FilePath, std::ios::binary | std::ios::ate);
			if (!File)
			{
				MAHO_CORE_ERROR(
					"Resource BulkData failed: id={} path=\"{}\" (open failed)",
					Id.Value,
					Path);
			}
			else
			{
				const std::streamoff Size = File.tellg();
				if (Size < 0)
				{
					MAHO_CORE_ERROR(
						"Resource BulkData failed: id={} path=\"{}\" (size failed)",
						Id.Value,
						Path);
				}
				else
				{
					File.seekg(0, std::ios::beg);
					Bytes.resize(static_cast<std::size_t>(Size));
					if (Size > 0 && !File.read(reinterpret_cast<char*>(Bytes.data()), Size))
					{
						Bytes.clear();
						MAHO_CORE_ERROR(
							"Resource BulkData failed: id={} path=\"{}\" (read failed)",
							Id.Value,
							Path);
					}
					else
					{
						bSuccess = true;
						MAHO_CORE_INFO(
							"Resource BulkData ready: id={} path=\"{}\" bytes={}",
							Id.Value,
							Path,
							Bytes.size());
					}
				}
			}
		}

		CompleteLoad(Id, bSuccess, std::move(Bytes));
	});

	return Id;
}

void FResourceServer::CompleteLoad(
	FResourceId Id,
	bool bSuccess,
	std::vector<std::uint8_t> BulkBytes)
{
	{
		std::lock_guard<std::mutex> Lock(RegistryMutex);
		const auto It = Registry.find(Id.Value);
		if (It == Registry.end())
		{
			return;
		}
		It->second.State = bSuccess ? EResourceLoadState::Ready : EResourceLoadState::Failed;
		It->second.BulkBytes = std::move(BulkBytes);
		It->second.bBulkTaken = false;
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

bool FResourceServer::TryTakeBulkData(FResourceId Id, FResourceBulkData& OutBulk)
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

	if (It->second.State != EResourceLoadState::Ready || It->second.bBulkTaken)
	{
		return false;
	}

	OutBulk.SourcePath = It->second.Path;
	OutBulk.Bytes = std::move(It->second.BulkBytes);
	It->second.bBulkTaken = true;
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

} // namespace Maho
