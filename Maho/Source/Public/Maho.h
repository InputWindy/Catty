#pragma once

// Umbrella header for game projects linking Maho.
// Entry point: also #include <EntryPoint.h> in exactly one game .cpp.
//
// Example:
// ```
//   #include <Maho.h>
//   #include <EntryPoint.h>
//
//   class FMyGameApp : public Maho::FApp;
//   Maho::FApp* Maho::CreateApplication() { return new FMyGameApp(); }
// ```
#include <Core/Export.h>
#include <Core/System/Log.h>
#include <Core/System/Fatal.h>
#include <Core/System/Timer.h>
#include <Core/System/ConfigFile.h>
#include <Core/Json.h>
#include <Core/Object/PoolAllocator.h>
#include <Core/System/ConsoleVariable.h>
#include <Core/System/Console.h>
#include <Core/Delegate.h>
#include <Core/Object/ObjectReflect.h>
#include <Core/Concurrent/WorkerPool.h>
#include <Core/Concurrent/AsyncTask.h>
#include <Core/Engine.h>
#include <Core/System/Paths.h>
#include <Core/Object/SoftObjectPath.h>
#include <Core/Sequencer/EngineExtension.h>
#include <Core/Application/App.h>
#include <Core/Extension/GC.h>
#include <Core/Extension/WorkerPool.h>
#include <Core/Extension/Platform.h>
#include <Core/Extension/Render.h>
#include <Core/Extension/Resource.h>
#include <Core/Extension/Script.h>
#include <Core/Extension/ScriptLayer.h>
#include <Core/Extension/EditorLayer.h>
#include <Core/System/PlatformWindow.h>
#include <Render/RHI/RHI.h>
#include <Render/RHI/RHIServer.h>
#include <Render/RenderServer.h>
#include <Render/Sequencer/RenderStage.h>
#include <Render/Sequencer/RenderExtension.h>
#include <Core/Object/Object.h>
#include <Core/Object/Package.h>
#include <Core/Server/ThreadedServer.h>
#include <Core/Server/ServerTask.h>
#include <Core/Server/TaskContext.h>
#include <Render/UI/ImGuiSystem.h>
#include <Render/UI/ImGuiTheme.h>

#if defined(MAHO_WITH_IMGUI)
#	include <imgui.h>
#	include <Render/UI/ImGuiExtensions.h>
#endif
