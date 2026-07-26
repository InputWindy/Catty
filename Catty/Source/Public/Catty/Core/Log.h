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
 * spdlog facade for Catty.
 * Use CATTY_CORE_* for engine code and CATTY_* for game code.
 */
class CATTY_API FLog
{
public:
	/** Create / replace loggers. Safe to call again (re-initializes). */
	static void Initialize(const FLogConfig& Config);

	static void Shutdown();

	[[nodiscard]] static bool IsInitialized();

	[[nodiscard]] static std::shared_ptr<spdlog::logger>& GetCoreLogger();
	[[nodiscard]] static std::shared_ptr<spdlog::logger>& GetClientLogger();
};

} // namespace Catty

// Engine (core) macros
#define CATTY_CORE_TRACE(...)    ::Catty::FLog::GetCoreLogger()->trace(__VA_ARGS__)
#define CATTY_CORE_DEBUG(...)    ::Catty::FLog::GetCoreLogger()->debug(__VA_ARGS__)
#define CATTY_CORE_INFO(...)     ::Catty::FLog::GetCoreLogger()->info(__VA_ARGS__)
#define CATTY_CORE_WARN(...)     ::Catty::FLog::GetCoreLogger()->warn(__VA_ARGS__)
#define CATTY_CORE_ERROR(...)    ::Catty::FLog::GetCoreLogger()->error(__VA_ARGS__)
#define CATTY_CORE_CRITICAL(...) ::Catty::FLog::GetCoreLogger()->critical(__VA_ARGS__)

// Game (client) macros
#define CATTY_TRACE(...)         ::Catty::FLog::GetClientLogger()->trace(__VA_ARGS__)
#define CATTY_DEBUG(...)         ::Catty::FLog::GetClientLogger()->debug(__VA_ARGS__)
#define CATTY_INFO(...)          ::Catty::FLog::GetClientLogger()->info(__VA_ARGS__)
#define CATTY_WARN(...)          ::Catty::FLog::GetClientLogger()->warn(__VA_ARGS__)
#define CATTY_ERROR(...)         ::Catty::FLog::GetClientLogger()->error(__VA_ARGS__)
#define CATTY_CRITICAL(...)      ::Catty::FLog::GetClientLogger()->critical(__VA_ARGS__)
