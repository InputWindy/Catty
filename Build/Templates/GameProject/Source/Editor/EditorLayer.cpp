#include "Editor/EditorLayer.h"

#include <imgui.h>

FEditorLayer::FEditorLayer()
	: Catty::FLayer("EditorLayer")
{
}

void FEditorLayer::OnUpdate(float /*DeltaSeconds*/)
{
	if (bShowDemoWindow)
	{
		ImGui::ShowDemoWindow(&bShowDemoWindow);
	}
}
