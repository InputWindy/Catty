#pragma once

#include <Core/Export.h>
#include <Core/Sequencer/EngineStage.h>

#include <cstdint>
#include <string>

namespace Catty
{

class FApp;

/** Ordering band for FApp RebuildOrder (Module before Layer before Overlay). */
enum class EExtensionPriority : std::uint8_t
{
	Module = 0,
	Layer = 1,
	Overlay = 2,
};

/**
 * Engine feature extension.
 * Ordering edges live on TDependsPack; FApp drives all stages via ExecuteStage.
 */
class CATTY_API IEngineExtension
{
public:
	virtual ~IEngineExtension() = default;

	[[nodiscard]] virtual const char* GetName() const = 0;

	[[nodiscard]] EExtensionPriority GetPriority() const { return Priority; }

	/**
	 * Lifecycle + frame body. Init-family may return false to abort startup.
	 * Frame / mount stages should return true.
	 */
	virtual bool ExecuteStage(EEngineStage Stage)
	{
		(void)Stage;
		return true;
	}

	/**
	 * True when this extension has no outstanding work (async loads, live objects, GPU, …).
	 */
	[[nodiscard]] virtual bool IsIdle() const
	{
		return true;
	}

	[[nodiscard]] EEngineStage GetCurrentStage() const { return CurrentStage; }

private:
	friend class FApp;

	void SetPriority(EExtensionPriority InPriority) { Priority = InPriority; }
	void SetCurrentStage(EEngineStage Stage) { CurrentStage = Stage; }

	EExtensionPriority Priority = EExtensionPriority::Module;
	EEngineStage CurrentStage = EEngineStage::COUNT;
};

/**
 * Named extension helper for game / editor slices (Priority Layer or Overlay).
 */
class CATTY_API FLayer : public IEngineExtension
{
public:
	explicit FLayer(std::string InName = "Layer")
		: Name(std::move(InName))
	{
	}

	~FLayer() override = default;

	FLayer(const FLayer&) = delete;
	FLayer& operator=(const FLayer&) = delete;

	[[nodiscard]] const char* GetName() const override { return Name.c_str(); }
	[[nodiscard]] const std::string& GetNameString() const { return Name; }

protected:
	std::string Name;
};

} // namespace Catty
