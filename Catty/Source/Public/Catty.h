#pragma once

// Umbrella header for game projects linking Catty.
// Entry point: also #include <EntryPoint.h> in exactly one game .cpp.
//
// Built-in modules (Platform / Render / GC / Resource) ship in Catty.dll and are
// registered by FApp::RegisterModules(). Optional Engine/Project plugins still
// link via Catty::Modules when present.
//
// Example:
// ```
//   #include <Catty.h>
//   #include <EntryPoint.h>
//
//   class FMyGameApp : public Catty::FApp; // Configure / PostInitialize
//   Catty::FApp* Catty::CreateApplication() { return new FMyGameApp(); }
// ```
#include <Core/Export.h>
#include <Core/Log.h>
#include <Core/Timer.h>
#include <Core/ConfigFile.h>
#include <Core/Json.h>
#include <Core/PoolAllocator.h>
#include <Core/ConsoleVariable.h>
#include <Core/ConsoleManager.h>
#include <Core/Delegate.h>
#include <Core/ObjectReflect.h>
#include <Core/WorkerPool.h>
#include <Core/AsyncTask.h>
#include <Core/Engine.h>
#include <Core/Module.h>
#include <Core/Layer.h>
#include <Core/App.h>
#include <Core/Modules/PlatformModule.h>
#include <Core/Modules/RenderModule.h>
#include <Core/Modules/GCModule.h>
#include <Core/Modules/ResourceModule.h>
#include <Core/PlatformWindow.h>
#include <Render/RHI/RHI.h>
#include <Render/RenderServer.h>
#include <Core/Resource/ResourceHandle.h>
#include <Core/Object.h>
#include <Core/Resource/Resource.h>
#include <Core/Resource/Package.h>
#include <Core/GCManager.h>
#include <Core/Resource/ResourceManager.h>
#include <Core/Layer/ScriptSystem.h>
#include <Core/Layer/ScriptLayer.h>
#include <Core/Server/ThreadedServer.h>
#include <Core/Server/ServerTask.h>
#include <Core/Server/TaskContext.h>
#include <Render/UI/ImGuiSystem.h>
#include <Render/UI/ImGuiTheme.h>

#if defined(CATTY_WITH_IMGUI)
#	include <imgui.h>
#	include <Render/UI/ImGuiExtensions.h>
#endif
