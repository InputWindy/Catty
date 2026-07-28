#pragma once

#include "Catty/Core/Export.h"

namespace Catty
{

class FApp;

/**
 * Optional helper for games that want the stock engine Module DAG.
 * FApp does not call this — invoke from FApp::RegisterModules() in the game subclass.
 *
 * Order / deps: Engine → Platform → Render → ImGui → GC → Resource → Worker.
 */
CATTY_API void RegisterEngineModules(FApp& App);

} // namespace Catty
