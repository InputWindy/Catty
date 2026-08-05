#pragma once

#include <Core/DependsPack.h>
#include <Core/Export.h>
#include <Core/TypeList.h>
#include <Render/RenderPipelineStage.h>

#include <cstdint>
#include <functional>
#include <typeindex>

namespace Maho
{

class FRenderServer;
class FRDGBuilder;

// ── Compile-time Feature dependency slot ──────────────────────────────

template <ERenderPipelineStage StageKey, typename DependsList = TTypeList<>>
struct TFeatureDependsOn
{
	static constexpr ERenderPipelineStage Key = StageKey;
	using FDependsList = DependsList;
};

template <typename... TSlots>
struct TFeatureDependsPack
{
	using FDependsPack = TFeatureDependsPack;
	static constexpr std::size_t NumSlots = sizeof...(TSlots);

	template <typename TVisitor>
	static constexpr void ForEachSlot(TVisitor&& Visitor)
	{
		if constexpr (sizeof...(TSlots) > 0)
			(Visitor(TSlots::Key, static_cast<typename TSlots::FDependsList*>(nullptr)), ...);
		else
			(void)Visitor;
	}

	template <ERenderPipelineStage Stage>
	static constexpr bool ParticipatesIn()
	{
		if constexpr (sizeof...(TSlots) > 0)
			return ((TSlots::Key == Stage) || ...);
		else
			return false;
	}
};

template <typename T, typename = void>
struct TResolveFeatureDependsPack
{
	using Type = TFeatureDependsPack<>;
};

template <typename T>
struct TResolveFeatureDependsPack<T, std::void_t<typename T::FDependsPack>>
{
	using Type = typename T::FDependsPack;
};

// ── IRenderFeature ────────────────────────────────────────────────────

class MAHO_API IRenderFeature
{
public:
	virtual ~IRenderFeature() = default;

	[[nodiscard]] virtual const char* GetName() const = 0;

	virtual bool OnRegister(FRenderServer& RenderServer) { (void)RenderServer; return true; }
	virtual void OnUnregister(FRenderServer& RenderServer) { (void)RenderServer; }

	/** Stage is an explicit parameter — aligned with IEngineExtension::ExecuteStage. */
	virtual void ExecuteStage(ERenderPipelineStage Stage, FRDGBuilder& GraphBuilder)
	{
		(void)Stage;
		(void)GraphBuilder;
	}

	[[nodiscard]] virtual bool ParticipatesInStage(ERenderPipelineStage Stage) const = 0;
	virtual void ForEachStageDep(ERenderPipelineStage Stage,
	                             const std::function<void(const std::type_index&)>& Visitor) const = 0;
};

// ── CRTP base with auto dispatch ─────────────────────────────────────

template <typename TDerived>
class TRenderFeatureBase : public IRenderFeature
{
public:
	explicit TRenderFeatureBase(const char* InName)
		: Name(InName ? InName : "RenderFeature")
	{
	}

	[[nodiscard]] const char* GetName() const override { return Name; }

	[[nodiscard]] bool ParticipatesInStage(ERenderPipelineStage Stage) const override
	{
		using Pack = typename TResolveFeatureDependsPack<TDerived>::Type;
		bool bFound = false;
		Pack::ForEachSlot([&](auto Key, auto*) {
			if (Key == Stage) bFound = true;
		});
		return bFound;
	}

	void ForEachStageDep(ERenderPipelineStage Stage,
	                     const std::function<void(const std::type_index&)>& Visitor) const override
	{
		using Pack = typename TResolveFeatureDependsPack<TDerived>::Type;
		Pack::ForEachSlot([&](auto Key, auto* DepsList) {
			if (Key == Stage)
			{
				TDispatchDepList(DepsList, Visitor);
			}
		});
	}

protected:
	const char* Name;

private:
	template <typename... TDepTypes>
	static void TDispatchDepList(TTypeList<TDepTypes...>*,
		const std::function<void(const std::type_index&)>& V)
	{
		if constexpr (sizeof...(TDepTypes) > 0)
		{
			(V(std::type_index(typeid(TDepTypes))), ...);
		}
		else
		{
			(void)V;
		}
	}
};

// ── Simple named feature ─────────────────────────────────────────────

class MAHO_API FRenderFeature : public TRenderFeatureBase<FRenderFeature>
{
public:
	explicit FRenderFeature(const char* InName) : TRenderFeatureBase<FRenderFeature>(InName) {}
};

} // namespace Maho
