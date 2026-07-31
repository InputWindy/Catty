#pragma once

#include <Core/Export.h>

namespace Catty
{

/**
 * Unified fatal path (tier A): stderr + Saved/Logs/Fatal.log + flush spdlog if live, then abort.
 * Safe before FApp/Log exist. Do not call from ordinary gameplay — use for contract failures only.
 */
[[noreturn]] CATTY_API void ReportFatal(const char* Message);

/** Install std::terminate handler once (call from process entry before CreateApplication). */
CATTY_API void InstallFatalHandlers();

} // namespace Catty
