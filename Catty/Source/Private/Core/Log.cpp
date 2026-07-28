#include <Core/Log.h>

#include <Core/App.h>
#include <Core/ConsoleManager.h>

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <vector>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Catty
{

namespace
{

constexpr const char* GLogPattern = "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v";

static TAutoConsoleVariable GCVarLogMaxFileBytes(
	"log.MaxFileBytes",
	5 * 1024 * 1024,
	"Rotating log file max size in bytes");

static TAutoConsoleVariable GCVarLogMaxFileCount(
	"log.MaxFileCount",
	3,
	"Rotating log file count");

std::shared_ptr<spdlog::logger> CreateLogger(
	const std::string& Name,
	const std::vector<spdlog::sink_ptr>& Sinks)
{
	if (auto Existing = spdlog::get(Name))
	{
		spdlog::drop(Name);
	}

	auto Logger = std::make_shared<spdlog::logger>(Name, Sinks.begin(), Sinks.end());
	Logger->set_pattern(GLogPattern);
	Logger->set_level(spdlog::level::trace);
	Logger->flush_on(spdlog::level::warn);
	spdlog::register_logger(Logger);
	return Logger;
}

} // namespace

FLog::~FLog()
{
	Shutdown();
}

void FLog::Initialize(const FLogConfig& Config)
{
	if (bInitialized)
	{
		Shutdown();
	}

	std::vector<spdlog::sink_ptr> Sinks;

	if (Config.bEnableConsole)
	{
		auto ConsoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		ConsoleSink->set_level(spdlog::level::trace);
		Sinks.push_back(ConsoleSink);
	}

	if (Config.bEnableFile)
	{
		namespace fs = std::filesystem;
		std::error_code ErrorCode;
		fs::create_directories(Config.LogDirectory, ErrorCode);

		const std::size_t MaxFileBytes = static_cast<std::size_t>(
			(std::max)(1024, GCVarLogMaxFileBytes.GetValue()));
		const std::size_t MaxFileCount = static_cast<std::size_t>(
			(std::max)(1, GCVarLogMaxFileCount.GetValue()));

		const fs::path LogFilePath = fs::path(Config.LogDirectory) / (Config.CoreLoggerName + ".log");
		auto FileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
			LogFilePath.string(),
			MaxFileBytes,
			MaxFileCount);
		FileSink->set_level(spdlog::level::trace);
		Sinks.push_back(FileSink);
	}

	if (Sinks.empty())
	{
		auto ConsoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		Sinks.push_back(ConsoleSink);
	}

	CoreLogger = CreateLogger(Config.CoreLoggerName, Sinks);
	ClientLogger = CreateLogger(Config.ClientLoggerName, Sinks);
	spdlog::set_default_logger(CoreLogger);

	bInitialized = true;
	CoreLogger->info(
		"Logging initialized (console={}, file={})",
		Config.bEnableConsole,
		Config.bEnableFile);
}

void FLog::Shutdown()
{
	if (!bInitialized)
	{
		return;
	}

	if (CoreLogger)
	{
		CoreLogger->info("Logging shutdown");
		CoreLogger->flush();
		spdlog::drop(CoreLogger->name());
		CoreLogger.reset();
	}
	if (ClientLogger)
	{
		ClientLogger->flush();
		spdlog::drop(ClientLogger->name());
		ClientLogger.reset();
	}

	bInitialized = false;
}

std::shared_ptr<spdlog::logger>& FLog::GetActiveCoreLogger()
{
	FLog& Log = GApp->GetLog();
	if (!Log.CoreLogger)
	{
		throw std::runtime_error("FLog: core logger not initialized (FApp::Initialize)");
	}
	return Log.CoreLogger;
}

std::shared_ptr<spdlog::logger>& FLog::GetActiveClientLogger()
{
	FLog& Log = GApp->GetLog();
	if (!Log.ClientLogger)
	{
		throw std::runtime_error("FLog: client logger not initialized (FApp::Initialize)");
	}
	return Log.ClientLogger;
}

} // namespace Catty
