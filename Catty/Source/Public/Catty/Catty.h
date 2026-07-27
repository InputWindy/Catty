#pragma once

// Umbrella header for game projects linking Catty.
// Entry point: also #include <Catty/EntryPoint.h> in exactly one game .cpp.
//
// Example:
// ```
//   #include <Catty/Catty.h>
//   #include <Catty/EntryPoint.h>
//
//   class FMyGameApp : public Catty::FApp; // Configure / PostInitialize
//   Catty::FApp* Catty::CreateApplication() { return new FMyGameApp(); }
// ```
#include "Catty/Core/Export.h"
#include "Catty/Core/Log.h"
#include "Catty/Core/Timer.h"
#include "Catty/Core/ConfigFile.h"
#include "Catty/Core/Json.h"
#include "Catty/Core/PoolAllocator.h"
#include "Catty/Core/ConsoleVariable.h"
#include "Catty/Core/ConsoleManager.h"
#include "Catty/Core/Delegate.h"
#include "Catty/Core/Reflect.h"
#include "Catty/Core/WorkerPool.h"
#include "Catty/Core/AsyncTask.h"
#include "Catty/Core/Engine.h"
#include "Catty/Core/Layer.h"
#include "Catty/Core/LayerStack.h"
#include "Catty/Core/App.h"
#include "Catty/Platform/PlatformWindow.h"
#include "Catty/RHI/RHI.h"
#include "Catty/Render/RenderServer.h"
#include "Catty/Resource/ResourceHandle.h"
#include "Catty/Resource/Object.h"
#include "Catty/Resource/Resource.h"
#include "Catty/Resource/Package.h"
#include "Catty/Resource/GCManager.h"
#include "Catty/Resource/ResourceManager.h"
#include "Catty/Script/ScriptSystem.h"
#include "Catty/Server/ThreadedServer.h"
#include "Catty/Server/ServerTask.h"
#include "Catty/Server/TaskContext.h"
#include "Catty/UI/ImGuiSystem.h"

#if defined(CATTY_WITH_IMGUI)
#	include <imgui.h>
#endif
