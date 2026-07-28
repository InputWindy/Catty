#include "Editor/EditorLayer.h"

#include <Core/App.h>

#include <imgui.h>

FEditorLayer::FEditorLayer()
	: Catty::FLayer("EditorLayer")
{
}

void FEditorLayer::OnUpdate(
	Catty::EModuleStage /*Stage*/,
	Catty::FApp& /*App*/,
	Catty::FStageContext& /*Ctx*/)
{
	if (ImGui::GetCurrentContext() == nullptr)
	{
		return;
	}

	if (bShowDemoWindow)
	{
		ImGui::ShowDemoWindow(&bShowDemoWindow);
	}
}
