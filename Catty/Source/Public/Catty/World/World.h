#pragma once

#include "Catty/Core/Export.h"

#include <cstdint>
#include <string>

namespace Catty
{

/**
 * Gameplay world container (entities / levels later).
 * Owned by FEngine; created empty at engine init.
 */
class CATTY_API FWorld
{
public:
	FWorld() = default;

	FWorld(const FWorld&) = delete;
	FWorld& operator=(const FWorld&) = delete;

	bool Initialize(std::string InName = "MainWorld");
	void Tick(float DeltaSeconds);
	void Shutdown();

	[[nodiscard]] bool IsInitialized() const { return bInitialized; }
	[[nodiscard]] const std::string& GetName() const { return Name; }
	[[nodiscard]] std::uint64_t GetTickCount() const { return TickCount; }

private:
	bool bInitialized = false;
	std::string Name;
	std::uint64_t TickCount = 0;
};

} // namespace Catty
