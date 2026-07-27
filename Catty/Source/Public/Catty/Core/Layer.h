#pragma once

#include "Catty/Core/Export.h"

#include <string>

namespace Catty
{

/**
 * Modular app slice with the same frame hooks as FApp.
 * Push onto FLayerStack from FApp (or game code) to compose update/render logic.
 *
 * Example:
 * ```
 *   class FWorldLayer : public Catty::FLayer
 *   {
 *   public:
 *       FWorldLayer() : Catty::FLayer("WorldLayer") {}
 *       virtual void OnUpdate(float DeltaSeconds) override { /* tick world */ }
 *   };
 *
 *   App.PushLayer(std::make_unique<FWorldLayer>());
 * ```
 */
class CATTY_API FLayer
{
public:
	explicit FLayer(std::string Name = "Layer");
	virtual ~FLayer();

	FLayer(const FLayer&) = delete;
	FLayer& operator=(const FLayer&) = delete;

	virtual void OnAttach() {}
	virtual void OnDetach() {}

	virtual void OnBeginFrame(float /*DeltaSeconds*/) {}
	virtual void OnProcessInput(float /*DeltaSeconds*/) {}
	virtual void OnFixedUpdate(float /*FixedDeltaSeconds*/) {}
	virtual void OnUpdate(float /*DeltaSeconds*/) {}
	virtual void OnLateUpdate(float /*DeltaSeconds*/) {}
	virtual void OnPreRender(float /*DeltaSeconds*/) {}
	virtual void OnRender(float /*DeltaSeconds*/) {}
	virtual void OnEndFrame(float /*DeltaSeconds*/) {}

	[[nodiscard]] const std::string& GetName() const { return Name; }

protected:
	std::string Name;
};

} // namespace Catty
