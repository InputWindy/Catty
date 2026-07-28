#pragma once

// Umbrella header for game projects linking Catty.
// Entry point: also #include <EntryPoint.h> in exactly one game .cpp.
//
// Plugin module headers (FPlatformModule, FRenderModule, ...) live in plugin DLLs.
// Link Catty::Modules (or CattyModules) in addition to Catty::Engine so those
// include paths and import libs resolve.
//
// Example:
// ```
//   #include <Catty.h>
//   #include <EntryPoint.h>
//
//   class FMyGameApp : public Catty::FApp; // Configure / PostInitialize
//   Catty::FApp* Catty::CreateApplication() { return new FMyGameApp(); }
// ```
#include "Core/Export.h"
#include "Core/Log.h"
#include "Core/Timer.h"
#include "Core/ConfigFile.h"
#include "Core/Json.h"
#include "Core/PoolAllocator.h"
#include "Core/ConsoleVariable.h"
#include "Core/ConsoleManager.h"
#include "Core/Delegate.h"
#include "Core/ObjectReflect.h"
#include "Core/WorkerPool.h"
#include "Core/AsyncTask.h"
#include "Core/Engine.h"
#include "Core/Module.h"
#include "Core/Layer.h"
#include "Core/App.h"
#include "PlatformModule.h"
#include "RenderModule.h"
#include "ImGuiModule.h"
#include "GCModule.h"
#include "ResourceModule.h"
#include "Platform/PlatformWindow.h"
#include "RHI/RHI.h"
#include "Render/RenderServer.h"
#include "Resource/ResourceHandle.h"
#include "Resource/Object.h"
#include "Resource/Resource.h"
#include "Resource/Package.h"
#include "Resource/GCManager.h"
#include "Resource/ResourceManager.h"
#include "Script/ScriptSystem.h"
#include "Script/ScriptLayer.h"
#include "Server/ThreadedServer.h"
#include "Server/ServerTask.h"
#include "Server/TaskContext.h"
#include "UI/ImGuiSystem.h"

#if defined(CATTY_WITH_IMGUI)
#	include <imgui.h>
#endif
