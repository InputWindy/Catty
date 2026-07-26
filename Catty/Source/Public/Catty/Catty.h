#pragma once

// Umbrella header for game projects linking Catty.
// Entry point: also #include <Catty/EntryPoint.h> in exactly one game .cpp.
#include "Catty/Core/Export.h"
#include "Catty/Core/Log.h"
#include "Catty/Core/Timer.h"
#include "Catty/Core/Engine.h"
#include "Catty/Core/App.h"
#include "Catty/Platform/PlatformWindow.h"
#include "Catty/Render/RenderServer.h"
#include "Catty/Resource/ResourceServer.h"
#include "Catty/Server/ThreadedServer.h"
#include "Catty/Server/ServerTask.h"
#include "Catty/Server/TaskContext.h"
