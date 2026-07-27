#include "Catty/Core/Layer.h"

namespace Catty
{

FLayer::FLayer(std::string InName)
	: Name(std::move(InName))
{
}

FLayer::~FLayer() = default;

} // namespace Catty
