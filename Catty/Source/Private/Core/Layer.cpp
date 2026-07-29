#include <Core/Layer.h>
#include <Core/Log.h>

#include <atomic>

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
	static std::atomic<int> LayerLogBudget{40};
	const int Left = LayerLogBudget.fetch_sub(1, std::memory_order_relaxed);
	if (Left > 0)
	{
		CATTY_CORE_WARN("[Layer] {} OnSequencerStage {}", Name, FrameStageName(Stage));
	}
}


} // namespace Catty
