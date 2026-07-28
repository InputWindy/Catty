#pragma once

#include "Catty/Core/Export.h"

#include <memory>
#include <string>

#if defined(_MSC_VER)
#	pragma warning(push)
#	pragma warning(disable : 4459) // fmt: 'formattable' hides global
#endif
#include <spdlog/spdlog.h>
#if defined(_MSC_VER)
#	pragma warning(pop)
#endif

namespace Catty
{

struct FLogConfig
{
	/** Core (engine) logger name. */
	std::string CoreLoggerName = "Catty";

	/** Client (game) logger name — typically ApplicationName. */
	std::string ClientLoggerName = "App";

	/** Directory for log files (UE-style: Saved/Logs). */
	std::string LogDirectory = "Saved/Logs";

	bool bEnableConsole = true;
	bool bEnableFile = true;
};

/**
 * App-owned spdlog facade. CATTY_* macros resolve via GApp->GetLog().
 */
class CATTY_API FLog
{
public:
	FLog() = default;
	~FLog();

	FLog(const FLog&) = delete;
	FLog& operator=(const FLog&) = delete;

	/** Create / replace this instance's loggers. Safe to call again. */
	void Initialize(const FLogConfig& Config);
	void Shutdown();

	[[nodiscard]] bool IsInitialized() const { return bInitialized; }

	[[nodiscard]] std::shared_ptr<spdlog::logger>& GetCoreLogger() { return CoreLogger; }
	[[nodiscard]] std::shared_ptr<spdlog::logger>& GetClientLogger() { return ClientLogger; }

	/** Macro helpers — forward to GApp->GetLog(). */
	[[nodiscard]] static std::shared_ptr<spdlog::logger>& GetActiveCoreLogger();
	[[nodiscard]] static std::shared_ptr<spdlog::logger>& GetActiveClientLogger();

private:
	std::shared_ptr<spdlog::logger> CoreLogger;
	std::shared_ptr<spdlog::logger> ClientLogger;
	bool bInitialized = false;
};

} // namespace Catty

#define CATTY_CORE_TRACE(...)    ::Catty::FLog::GetActiveCoreLogger()->trace(__VA_ARGS__)
#define CATTY_CORE_DEBUG(...)    ::Catty::FLog::GetActiveCoreLogger()->debug(__VA_ARGS__)
#define CATTY_CORE_INFO(...)     ::Catty::FLog::GetActiveCoreLogger()->info(__VA_ARGS__)
#define CATTY_CORE_WARN(...)     ::Catty::FLog::GetActiveCoreLogger()->warn(__VA_ARGS__)
#define CATTY_CORE_ERROR(...)    ::Catty::FLog::GetActiveCoreLogger()->error(__VA_ARGS__)
#define CATTY_CORE_CRITICAL(...) ::Catty::FLog::GetActiveCoreLogger()->critical(__VA_ARGS__)

#define CATTY_TRACE(...)         ::Catty::FLog::GetActiveClientLogger()->trace(__VA_ARGS__)
#define CATTY_DEBUG(...)         ::Catty::FLog::GetActiveClientLogger()->debug(__VA_ARGS__)
#define CATTY_INFO(...)          ::Catty::FLog::GetActiveClientLogger()->info(__VA_ARGS__)
#define CATTY_WARN(...)          ::Catty::FLog::GetActiveClientLogger()->warn(__VA_ARGS__)
#define CATTY_ERROR(...)         ::Catty::FLog::GetActiveClientLogger()->error(__VA_ARGS__)
#define CATTY_CRITICAL(...)      ::Catty::FLog::GetActiveClientLogger()->critical(__VA_ARGS__)
