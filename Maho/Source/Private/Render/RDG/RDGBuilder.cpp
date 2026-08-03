#include <Render/RDG/RDGBuilder.h>

#include <Core/System/Log.h>
#include <Render/RHI/RHI.h>

#include <algorithm>
#include <queue>

namespace Maho
{

FRDGBuilder::FRDGBuilder(IRHI* InRHI)
	: RHI(InRHI)
{
}

FRDGBuilder::~FRDGBuilder()
{
}

// Resource Registration

FRDGBuffer* FRDGBuilder::RegisterExternalBuffer(
	FRHIBuffer* Buffer,
	ERHIResourceState InitialState,
	const char* Name)
{
	auto* Res = new FRDGBuffer(Name, Buffer->GetDesc(), true, false);
	Res->SetRHI(Buffer);
	Res->CurrentState = InitialState;
	OwnedResources.emplace_back(Res);
	NamedResources[Name] = Res;
	return Res;
}

FRDGTexture* FRDGBuilder::RegisterExternalTexture(
	FRHITexture* Texture,
	ERHIResourceState InitialState,
	const char* Name)
{
	auto* Res = new FRDGTexture(Name, Texture->GetDesc(), true, false);
	Res->SetRHI(Texture);
	Res->CurrentState = InitialState;
	OwnedResources.emplace_back(Res);
	NamedResources[Name] = Res;
	return Res;
}

// Transient Resource Creation

FRDGBuffer* FRDGBuilder::CreateBuffer(const FRHIBufferDesc& Desc, const char* Name)
{
	auto* Res = new FRDGBuffer(Name, Desc, false, true);
	OwnedResources.emplace_back(Res);
	NamedResources[Name] = Res;
	return Res;
}

FRDGTexture* FRDGBuilder::CreateTexture(const FRHITextureDesc& Desc, const char* Name)
{
	auto* Res = new FRDGTexture(Name, Desc, false, true);
	OwnedResources.emplace_back(Res);
	NamedResources[Name] = Res;
	return Res;
}

// Cross-Feature Export / Import

void FRDGBuilder::Export(FRDGResource* Resource, const char* Name)
{
	if (Resource == nullptr) return;
	ExportedResources[Name] = Resource;
	NamedResources[Name] = Resource;
}

FRDGResource* FRDGBuilder::Import(const char* Name) const
{
	auto It = ExportedResources.find(Name);
	return It != ExportedResources.end() ? It->second : nullptr;
}

// Pass Declaration

FRDGPass& FRDGBuilder::AddRasterPass(const char* Name, int32_t Layer)
{
	auto P = std::make_unique<FRDGPass>(Name, ERDGPassType::Raster);
	P->SetLayer(Layer);
	FRDGPass& Ref = *P;
	Passes.push_back(P.get());
	OwnedPasses.push_back(std::move(P));
	return Ref;
}

FRDGPass& FRDGBuilder::AddComputePass(const char* Name, int32_t Layer)
{
	auto P = std::make_unique<FRDGPass>(Name, ERDGPassType::Compute);
	P->SetLayer(Layer);
	FRDGPass& Ref = *P;
	Passes.push_back(P.get());
	OwnedPasses.push_back(std::move(P));
	return Ref;
}

FRDGPass& FRDGBuilder::AddCopyPass(const char* Name, int32_t Layer)
{
	auto P = std::make_unique<FRDGPass>(Name, ERDGPassType::Copy);
	P->SetLayer(Layer);
	FRDGPass& Ref = *P;
	Passes.push_back(P.get());
	OwnedPasses.push_back(std::move(P));
	return Ref;
}

void FRDGBuilder::Read(FRDGPass& Pass, FRDGResource* Resource, ERHIResourceState State)
{
	Pass.AddRead(Resource, State);
}

void FRDGBuilder::Write(FRDGPass& Pass, FRDGResource* Resource, ERHIResourceState State)
{
	Pass.AddWrite(Resource, State);
}

FRDGResource* FRDGBuilder::GetResource(const char* Name) const
{
	auto It = NamedResources.find(Name);
	return It != NamedResources.end() ? It->second : nullptr;
}

// Compile

void FRDGBuilder::Compile()
{
	CompiledPasses.clear();
	TransientPool.Reset();
	if (Passes.empty()) return;
	CollectResourceLifetimes();
	AllocateTransientResources();
	SortPasses();
	DeriveBarriers();
}

void FRDGBuilder::CollectResourceLifetimes()
{
	Lifetimes.clear();
	for (auto& Res : OwnedResources)
	{
		FRDGResource* R = Res.get();
		FResourceLifetime LT;
		LT.Resource = R;
		if (R->IsExternal()) { LT.FirstUse = 0; LT.LastUse = UINT32_MAX; }
		else { LT.FirstUse = UINT32_MAX; LT.LastUse = 0; }
		Lifetimes[R] = LT;
	}
	for (std::size_t i = 0; i < Passes.size(); ++i)
	{
		FRDGPass* Pass = Passes[i];
		std::uint32_t Idx = static_cast<std::uint32_t>(i);
		for (const auto& Acc : Pass->GetReads())
		{
			auto It = Lifetimes.find(Acc.Resource);
			if (It != Lifetimes.end() && !Acc.Resource->IsExternal())
			{
				if (Idx < It->second.FirstUse) It->second.FirstUse = Idx;
				if (Idx > It->second.LastUse)  It->second.LastUse = Idx;
			}
		}
		for (const auto& Acc : Pass->GetWrites())
		{
			auto It = Lifetimes.find(Acc.Resource);
			if (It != Lifetimes.end() && !Acc.Resource->IsExternal())
			{
				if (Idx < It->second.FirstUse) It->second.FirstUse = Idx;
				if (Idx > It->second.LastUse)  It->second.LastUse = Idx;
			}
		}
	}
}

void FRDGBuilder::AllocateTransientResources()
{
	for (auto& Pair : Lifetimes)
	{
		FRDGResource* Res = Pair.first;
		FResourceLifetime& LT = Pair.second;
		if (!Res->IsTransient()) continue;
		if (LT.FirstUse > LT.LastUse) continue;
		if (auto* Buf = dynamic_cast<FRDGBuffer*>(Res))
		{
			FRHIBuffer* RHIRes = TransientPool.AllocateBuffer(RHI, Buf->GetDesc(), LT.FirstUse, LT.LastUse);
			Buf->SetRHI(RHIRes);
		}
		else if (auto* Tex = dynamic_cast<FRDGTexture*>(Res))
		{
			FRHITexture* RHIRes = TransientPool.AllocateTexture(RHI, Tex->GetDesc(), LT.FirstUse, LT.LastUse);
			Tex->SetRHI(RHIRes);
		}
	}
}

void FRDGBuilder::SortPasses()
{
	std::unordered_map<FRDGResource*, std::size_t> LastWritePass;
	std::unordered_map<FRDGPass*, std::vector<FRDGPass*>> Deps;
	std::unordered_map<FRDGPass*, int32_t> InDegree;

	for (auto* P : Passes) InDegree.try_emplace(P, 0);

	for (std::size_t i = 0; i < Passes.size(); ++i)
	{
		FRDGPass* Pass = Passes[i];
		for (const auto& ReadAcc : Pass->GetReads())
		{
			auto It = LastWritePass.find(ReadAcc.Resource);
			if (It != LastWritePass.end() && It->second < i)
			{
				FRDGPass* Pred = Passes[It->second];
				Deps[Pred].push_back(Pass);
				InDegree[Pass]++;
			}
		}
		for (const auto& WriteAcc : Pass->GetWrites())
		{
			LastWritePass[WriteAcc.Resource] = i;
		}
	}

	// Kahn + layer tie-break
	std::vector<FRDGPass*> Sorted;
	std::vector<FRDGPass*> Ready;
	for (auto* P : Passes) if (InDegree[P] == 0) Ready.push_back(P);

	auto LayerCmp = [](FRDGPass* A, FRDGPass* B) { return A->GetLayer() < B->GetLayer(); };
	std::sort(Ready.begin(), Ready.end(), LayerCmp);

	while (!Ready.empty())
	{
		FRDGPass* P = Ready.front();
		Ready.erase(Ready.begin());
		Sorted.push_back(P);
		for (auto* Next : Deps[P])
			if (--InDegree[Next] == 0) Ready.push_back(Next);
		std::sort(Ready.begin(), Ready.end(), LayerCmp);
	}

	if (Sorted.size() != Passes.size())
	{
		MAHO_CORE_WARN("FRDGBuilder: circular dependency, fallback to declaration order");
		Sorted = Passes;
	}
	Passes = std::move(Sorted);
}

void FRDGBuilder::DeriveBarriers()
{
	CompiledPasses.clear();
	std::unordered_map<FRDGResource*, ERHIResourceState> CurrentStates;

	for (FRDGPass* Pass : Passes)
	{
		FCompiledPass CP;
		CP.Pass = Pass;

		for (const auto& ReadAcc : Pass->GetReads())
		{
			FRDGResource* Res = ReadAcc.Resource;
			if (Res == nullptr) continue;
			ERHIResourceState Cur = Res->CurrentState;
			auto It = CurrentStates.find(Res);
			if (It != CurrentStates.end()) Cur = It->second;
			if (Cur != ReadAcc.RequiredState && ReadAcc.RequiredState != ERHIResourceState::Common)
			{
				CP.PreBarriers.push_back({Res, ReadAcc.RequiredState});
				CurrentStates[Res] = ReadAcc.RequiredState;
			}
		}

		for (const auto& WriteAcc : Pass->GetWrites())
		{
			FRDGResource* Res = WriteAcc.Resource;
			if (Res == nullptr) continue;
			ERHIResourceState Cur = Res->CurrentState;
			auto It = CurrentStates.find(Res);
			if (It != CurrentStates.end()) Cur = It->second;
			if (Cur != WriteAcc.RequiredState && WriteAcc.RequiredState != ERHIResourceState::Common)
			{
				CP.PreBarriers.push_back({Res, WriteAcc.RequiredState});
				CurrentStates[Res] = WriteAcc.RequiredState;
			}
		}

		CompiledPasses.push_back(std::move(CP));
	}
}

// Execute

void FRDGBuilder::Execute()
{
	if (CompiledPasses.empty()) return;

	for (FCompiledPass& CP : CompiledPasses)
	{
		FRDGPass* Pass = CP.Pass;
		if (Pass == nullptr || !Pass->GetExecute()) continue;

		ERHICommandListType CmdType =
			(Pass->GetType() == ERDGPassType::Compute)
				? ERHICommandListType::Compute
				: ERHICommandListType::Graphics;

		FRHICommandList* Cmd = RHI->CreateCommandList(CmdType);
		if (Cmd == nullptr)
		{
			MAHO_CORE_ERROR("FRDGBuilder: failed to create command list for pass '{}'", Pass->GetName());
			continue;
		}

		Cmd->Begin();

		for (const auto& Barrier : CP.PreBarriers)
		{
			FRDGResource* Res = Barrier.first;
			ERHIResourceState Target = Barrier.second;
			if (auto* Buf = dynamic_cast<FRDGBuffer*>(Res))
			{
				if (auto* RHIBuf = Buf->GetRHI())
					Cmd->TransitionBuffer(RHIBuf, Res->CurrentState, Target);
			}
			else if (auto* Tex = dynamic_cast<FRDGTexture*>(Res))
			{
				if (auto* RHITex = Tex->GetRHI())
					Cmd->TransitionTexture(RHITex, Res->CurrentState, Target);
			}
		}

		Pass->GetExecute()(*Cmd);
		Cmd->End();

		if (Pass->GetType() == ERDGPassType::Compute)
			RHI->GetComputeQueue().Submit(&Cmd, 1, nullptr, 0, nullptr, 0, nullptr);
		else
			RHI->GetGraphicsQueue().Submit(&Cmd, 1, nullptr, 0, nullptr, 0, nullptr);

		RHI->DestroyCommandList(Cmd);
	}
}

} // namespace Maho
