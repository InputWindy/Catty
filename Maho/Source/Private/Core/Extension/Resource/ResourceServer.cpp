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
	for (auto& Pair : Registry)
	{
		if (Pair.second.Handle.IsValid() && Pair.second.Handle.IsInProgress())
		{
			SetTransferHandleState(Pair.second.Handle, ETransferState::Failed);
		}
	}
	Registry.clear();
	MAHO_CORE_INFO("ResourceServer shut down");
}

FTransferHandle FResourceServer::RequestLoad(std::string Path)
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

	FTransferHandle Handle = AllocateTransferHandle(ETransferState::InProgress);
	if (!Handle.IsValid())
	{
		return {};
	}

	{
		std::lock_guard<std::mutex> Lock(RegistryMutex);
		FResourceRecord Record;
		Record.Path = Path;
		Record.Handle = Handle;
		Registry.emplace(Handle.Id, std::move(Record));
	}

	Enqueue([this, Handle, Path = std::move(Path)](FThreadedServer& /*Server*/)
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
				Handle.Id,
				Path);
		}
		else
		{
			std::ifstream File(FilePath, std::ios::binary | std::ios::ate);
			if (!File)
			{
				MAHO_CORE_ERROR(
					"Resource BulkData failed: id={} path=\"{}\" (open failed)",
					Handle.Id,
					Path);
			}
			else
			{
				const std::streamoff Size = File.tellg();
				if (Size < 0)
				{
					MAHO_CORE_ERROR(
						"Resource BulkData failed: id={} path=\"{}\" (size failed)",
						Handle.Id,
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
							Handle.Id,
							Path);
					}
					else
					{
						bSuccess = true;
						MAHO_CORE_INFO(
							"Resource BulkData ready: id={} path=\"{}\" bytes={}",
							Handle.Id,
							Path,
							Bytes.size());
					}
				}
			}
		}

		CompleteLoad(Handle, bSuccess, std::move(Bytes));
	});

	return Handle;
}

void FResourceServer::CompleteLoad(
	FTransferHandle Handle,
	bool bSuccess,
	std::vector<std::uint8_t> BulkBytes)
{
	{
		std::lock_guard<std::mutex> Lock(RegistryMutex);
		const auto It = Registry.find(Handle.Id);
		if (It == Registry.end())
		{
			return;
		}
		It->second.BulkBytes = std::move(BulkBytes);
		It->second.bBulkTaken = false;
		It->second.bComplete = true;
		SetTransferHandleState(
			Handle,
			bSuccess ? ETransferState::Succeeded : ETransferState::Failed);
	}
	LoadCv.notify_all();
}

bool FResourceServer::TryTakeBulkData(FTransferHandle Handle, FResourceBulkData& OutBulk)
{
	if (!Handle.IsValid() || !Handle.HasSucceeded())
	{
		return false;
	}

	std::lock_guard<std::mutex> Lock(RegistryMutex);
	const auto It = Registry.find(Handle.Id);
	if (It == Registry.end() || It->second.bBulkTaken)
	{
		return false;
	}

	OutBulk.SourcePath = It->second.Path;
	OutBulk.Bytes = std::move(It->second.BulkBytes);
	It->second.bBulkTaken = true;
	return true;
}

void FResourceServer::Flush(FTransferHandle Handle)
{
	if (!Handle.IsValid())
	{
		return;
	}

	std::unique_lock<std::mutex> Lock(RegistryMutex);
	LoadCv.wait(Lock, [this, Handle]()
	{
		const auto It = Registry.find(Handle.Id);
		if (It == Registry.end())
		{
			return true;
		}
		return It->second.bComplete;
	});
}

void FResourceServer::Release(FTransferHandle Handle)
{
	if (!Handle.IsValid())
	{
		return;
	}

	{
		std::lock_guard<std::mutex> Lock(RegistryMutex);
		Registry.erase(Handle.Id);
	}
	LoadCv.notify_all();
}

bool FResourceServer::HasPendingLoads() const
{
	std::lock_guard<std::mutex> Lock(RegistryMutex);
	for (const auto& Pair : Registry)
	{
		if (!Pair.second.bComplete)
		{
			return true;
		}
	}
	return false;
}

} // namespace Maho
