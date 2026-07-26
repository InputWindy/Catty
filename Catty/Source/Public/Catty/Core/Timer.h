#pragma once

#include "Catty/Core/Export.h"

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Catty
{

/** One named scope's accumulated timing stats. */
struct FTimerScopeSample
{
	std::string Name;
	std::uint64_t CallCount = 0;
	double TotalMilliseconds = 0.0;
	double MinMilliseconds = 0.0;
	double MaxMilliseconds = 0.0;
	double LastMilliseconds = 0.0;

	[[nodiscard]] double AverageMilliseconds() const
	{
		return CallCount > 0 ? (TotalMilliseconds / static_cast<double>(CallCount)) : 0.0;
	}
};

/**
 * Uniform timer query payload (no polymorphism).
 * One package per category name — suitable for log and future UI.
 */
struct CATTY_API FTimerDataPackage
{
	/** Category name passed to CATTY_SCOPED_TIMER, e.g. "Engine" / "Render". */
	std::string SourceName;
	std::vector<FTimerScopeSample> Samples;

	[[nodiscard]] std::string Serialize() const;
};

/**
 * App-owned multi-category timer.
 * Categories are string keys (e.g. "Engine" / "Render" / "Game");
 * macros record into the active instance set by FApp::Run.
 */
class CATTY_API FTimer
{
public:
	FTimer() = default;
	~FTimer();

	FTimer(const FTimer&) = delete;
	FTimer& operator=(const FTimer&) = delete;

	/** Bound for the lifetime of FApp::Run so CATTY_SCOPED_TIMER can find it. */
	void MakeActive();
	void ClearActive();

	[[nodiscard]] static FTimer* TryGet();
	[[nodiscard]] static FTimer& Get();

	void Record(const char* CategoryName, const char* ScopeName, double ElapsedMilliseconds);

	void Reset(const char* CategoryName);
	void ResetAll();

	[[nodiscard]] bool TryQuery(const char* CategoryName, FTimerDataPackage& OutPackage) const;
	[[nodiscard]] std::vector<FTimerDataPackage> QueryAll() const;

private:
	struct FScopeAccum
	{
		std::uint64_t CallCount = 0;
		double TotalMilliseconds = 0.0;
		double MinMilliseconds = 0.0;
		double MaxMilliseconds = 0.0;
		double LastMilliseconds = 0.0;
	};

	mutable std::mutex Mutex;
	std::unordered_map<std::string, std::unordered_map<std::string, FScopeAccum>> Categories;

	static FTimer* GActive;
};

/**
 * RAII scope timer. Prefer CATTY_SCOPED_TIMER(CategoryName, ScopeName).
 */
class CATTY_API FScopedTimer
{
public:
	FScopedTimer(const char* CategoryName, const char* ScopeName);
	~FScopedTimer();

	FScopedTimer(const FScopedTimer&) = delete;
	FScopedTimer& operator=(const FScopedTimer&) = delete;

private:
	FTimer* Timer = nullptr;
	const char* CategoryName = nullptr;
	const char* ScopeName = nullptr;
	std::chrono::steady_clock::time_point StartTime{};
	bool bActive = false;
};

} // namespace Catty

#define CATTY_TIMER_CONCAT_INNER(A, B) A##B
#define CATTY_TIMER_CONCAT(A, B) CATTY_TIMER_CONCAT_INNER(A, B)

/**
 * Time the enclosing scope under a category on the App-owned FTimer.
 * Example: CATTY_SCOPED_TIMER("Engine", "FApp::Tick");
 */
#define CATTY_SCOPED_TIMER(CategoryName, ScopeName) \
	::Catty::FScopedTimer CATTY_TIMER_CONCAT(_CattyScopedTimer_, __LINE__)(CategoryName, ScopeName)
