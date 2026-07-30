#pragma once

#include <Core/Delegate.h>
#include <Core/Export.h>
#include <Core/FrameStage.h>

#include <string>

namespace Catty
{

class FApp;

/**
 * Game / editor slice registered on the Layer sequencer (PushLayer → RequestAdd).
 * Pipeline work: OnSequencerStage. Enter/leave stack events: Attach()/Detach().
 */
class CATTY_API FLayer
{
public:
	CATTY_DECLARE_MULTICAST_DELEGATE_OneParam(FOnAttach, FLayer&);
	CATTY_DECLARE_MULTICAST_DELEGATE_OneParam(FOnDetach, FLayer&);

	explicit FLayer(std::string Name = "Layer");
	virtual ~FLayer();

	FLayer(const FLayer&) = delete;
	FLayer& operator=(const FLayer&) = delete;

	void Attach();
	void Detach();

	[[nodiscard]] bool IsAttached() const { return bAttached; }

	[[nodiscard]] FOnAttach& GetOnAttach() { return AttachEvent; }
	[[nodiscard]] const FOnAttach& GetOnAttach() const { return AttachEvent; }
	[[nodiscard]] FOnDetach& GetOnDetach() { return DetachEvent; }
	[[nodiscard]] const FOnDetach& GetOnDetach() const { return DetachEvent; }

	virtual void OnAttach() {}
	virtual void OnDetach() {}

	/** Layer sequencer stage body (PreferMainThread defaults true). */
	[[nodiscard]] virtual bool PreferMainThread() const { return true; }

	virtual void OnSequencerStage(EFrameStage Stage);

	[[nodiscard]] const std::string& GetName() const { return Name; }

protected:
	std::string Name;

private:
	bool bAttached = false;
	FOnAttach AttachEvent;
	FOnDetach DetachEvent;
};

} // namespace Catty
