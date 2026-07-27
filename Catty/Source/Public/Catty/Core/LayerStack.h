#pragma once

#include "Catty/Core/Export.h"
#include "Catty/Core/Layer.h"

#include <memory>
#include <vector>

namespace Catty
{

/**
 * Ordered stack of layers + overlays.
 * Layers sit below overlays; overlays are typically UI / editor tooling.
 *
 * Example:
 * ```
 *   Stack.PushLayer(std::make_unique<FWorldLayer>());
 *   Stack.PushOverlay(std::make_unique<FEditorLayer>());
 *   Stack.Update(DeltaSeconds); // world then editor
 *   Stack.ProcessInput(DeltaSeconds); // overlays first
 * ```
 */
class CATTY_API FLayerStack
{
public:
	FLayerStack() = default;
	~FLayerStack();

	FLayerStack(const FLayerStack&) = delete;
	FLayerStack& operator=(const FLayerStack&) = delete;

	void PushLayer(std::unique_ptr<FLayer> Layer);
	void PushOverlay(std::unique_ptr<FLayer> Overlay);

	/** Detach and destroy every layer (call before tearing down engine systems). */
	void Clear();

	void BeginFrame(float DeltaSeconds);
	/** Overlays first, then layers (UI / editor can consume input before game layers). */
	void ProcessInput(float DeltaSeconds);
	void FixedUpdate(float FixedDeltaSeconds);
	void Update(float DeltaSeconds);
	void LateUpdate(float DeltaSeconds);
	void PreRender(float DeltaSeconds);
	void Render(float DeltaSeconds);
	void EndFrame(float DeltaSeconds);

	[[nodiscard]] std::size_t GetLayerCount() const { return Layers.size(); }

private:
	std::vector<std::unique_ptr<FLayer>> Layers;
	/** Index of the first overlay (layers occupy [0, LayerInsertIndex)). */
	std::size_t LayerInsertIndex = 0;
};

} // namespace Catty
