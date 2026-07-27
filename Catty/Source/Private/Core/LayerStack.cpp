#include "Catty/Core/LayerStack.h"

#include "Catty/Core/Log.h"

namespace Catty
{

FLayerStack::~FLayerStack()
{
	Clear();
}

void FLayerStack::PushLayer(std::unique_ptr<FLayer> Layer)
{
	if (!Layer)
	{
		return;
	}

	FLayer* Raw = Layer.get();
	Layers.emplace(Layers.begin() + static_cast<std::ptrdiff_t>(LayerInsertIndex), std::move(Layer));
	++LayerInsertIndex;
	Raw->OnAttach();
	CATTY_CORE_INFO("Layer attached: {}", Raw->GetName());
}

void FLayerStack::PushOverlay(std::unique_ptr<FLayer> Overlay)
{
	if (!Overlay)
	{
		return;
	}

	FLayer* Raw = Overlay.get();
	Layers.emplace_back(std::move(Overlay));
	Raw->OnAttach();
	CATTY_CORE_INFO("Overlay attached: {}", Raw->GetName());
}

void FLayerStack::Clear()
{
	for (auto It = Layers.rbegin(); It != Layers.rend(); ++It)
	{
		if (*It)
		{
			(*It)->OnDetach();
		}
	}
	Layers.clear();
	LayerInsertIndex = 0;
}

void FLayerStack::BeginFrame(float DeltaSeconds)
{
	for (std::unique_ptr<FLayer>& Layer : Layers)
	{
		Layer->OnBeginFrame(DeltaSeconds);
	}
}

void FLayerStack::ProcessInput(float DeltaSeconds)
{
	for (auto It = Layers.rbegin(); It != Layers.rend(); ++It)
	{
		(*It)->OnProcessInput(DeltaSeconds);
	}
}

void FLayerStack::FixedUpdate(float FixedDeltaSeconds)
{
	for (std::unique_ptr<FLayer>& Layer : Layers)
	{
		Layer->OnFixedUpdate(FixedDeltaSeconds);
	}
}

void FLayerStack::Update(float DeltaSeconds)
{
	for (std::unique_ptr<FLayer>& Layer : Layers)
	{
		Layer->OnUpdate(DeltaSeconds);
	}
}

void FLayerStack::LateUpdate(float DeltaSeconds)
{
	for (std::unique_ptr<FLayer>& Layer : Layers)
	{
		Layer->OnLateUpdate(DeltaSeconds);
	}
}

void FLayerStack::PreRender(float DeltaSeconds)
{
	for (std::unique_ptr<FLayer>& Layer : Layers)
	{
		Layer->OnPreRender(DeltaSeconds);
	}
}

void FLayerStack::Render(float DeltaSeconds)
{
	for (std::unique_ptr<FLayer>& Layer : Layers)
	{
		Layer->OnRender(DeltaSeconds);
	}
}

void FLayerStack::EndFrame(float DeltaSeconds)
{
	for (std::unique_ptr<FLayer>& Layer : Layers)
	{
		Layer->OnEndFrame(DeltaSeconds);
	}
}

} // namespace Catty
