#include "Catty/Core/Log.h"

#include <filesystem>
#include <vector>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace Catty
{
namespace
{

std::shared_ptr<spdlog::logger> GCoreLogger;
std::shared_ptr<spdlog::logger> GClientLogger;
bool GbLogInitialized = false;

constexpr const char* GLogPattern = "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v";
constexpr std::size_t GMaxFileBytes = 5 * 1024 * 1024;
constexpr std::size_t GMaxFileCount = 3;

std::shared_ptr<spdlog::logger> CreateLogger(
	const std::string& Name,
	const std::vector<spdlog::sink_ptr>& Sinks)
{
	auto Logger = std::make_shared<spdlog::logger>(Name, Sinks.begin(), Sinks.end());
	Logger->set_pattern(GLogPattern);
	Logger->set_level(spdlog::level::trace);
	Logger->flush_on(spdlog::level::warn);
	spdlog::register_logger(Logger);
	return Logger;
}

} // namespace

void FLog::Initialize(const FLogConfig& Config)
{
	if (GbLogInitialized)
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

		const fs::path LogFilePath = fs::path(Config.LogDirectory) / (Config.CoreLoggerName + ".log");
		auto FileSink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
			LogFilePath.string(),
			GMaxFileBytes,
			GMaxFileCount);
		FileSink->set_level(spdlog::level::trace);
		Sinks.push_back(FileSink);
	}

	if (Sinks.empty())
	{
		auto ConsoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
		Sinks.push_back(ConsoleSink);
	}

	GCoreLogger = CreateLogger(Config.CoreLoggerName, Sinks);
	GClientLogger = CreateLogger(Config.ClientLoggerName, Sinks);
	spdlog::set_default_logger(GCoreLogger);

	GbLogInitialized = true;
	GCoreLogger->info("Logging initialized (console={}, file={})",
		Config.bEnableConsole,
		Config.bEnableFile);
}

void FLog::Shutdown()
{
	if (!GbLogInitialized)
	{
		return;
	}

	if (GCoreLogger)
	{
		GCoreLogger->info("Logging shutdown");
		GCoreLogger->flush();
	}
	if (GClientLogger)
	{
		GClientLogger->flush();
	}

	spdlog::shutdown();
	GCoreLogger.reset();
	GClientLogger.reset();
	GbLogInitialized = false;
}

bool FLog::IsInitialized()
{
	return GbLogInitialized;
}

std::shared_ptr<spdlog::logger>& FLog::GetCoreLogger()
{
	return GCoreLogger;
}

std::shared_ptr<spdlog::logger>& FLog::GetClientLogger()
{
	return GClientLogger;
}

} // namespace Catty
