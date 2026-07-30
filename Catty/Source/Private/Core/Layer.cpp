#include <Core/Layer.h>

namespace Catty
{

FLayer::FLayer(std::string InName)
	: Name(std::move(InName))
{
}

FLayer::~FLayer()
{
	Detach();
}

void FLayer::Attach()
{
	if (bAttached)
	{
		return;
	}
	bAttached = true;
	AttachEvent.Broadcast(*this);
	OnAttach();
}

void FLayer::Detach()
{
	if (!bAttached)
	{
		return;
	}
	bAttached = false;
	DetachEvent.Broadcast(*this);
	OnDetach();
}

void FLayer::OnSequencerStage(EFrameStage Stage)
{
	// Attach/Detach frame stages are lockstep sync points only.
	// Stack enter/leave is Attach()/Detach() (PushLayer / FlushPendingRemoves).
	(void)Stage;
}

} // namespace Catty
