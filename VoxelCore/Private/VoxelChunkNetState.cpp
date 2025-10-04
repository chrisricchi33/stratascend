#include "VoxelChunkNetState.h"
#include "Net/UnrealNetwork.h"
#include "VoxelWorldManager.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"

AVoxelChunkNetState::AVoxelChunkNetState()
{
    bReplicates = true;
    SetReplicateMovement(false);

    SetNetUpdateFrequency(15.f);
    SetMinNetUpdateFrequency(5.f);
}

void AVoxelChunkNetState::BeginPlay()
{
    Super::BeginPlay();
    // IMPORTANT: set owner on both server and clients so FastArray callbacks fire on clients
    Cells.Owner = this;
}

void AVoxelChunkNetState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(AVoxelChunkNetState, ChunkXZ);
    DOREPLIFETIME(AVoxelChunkNetState, Cells);
}

// Optional: batch onrep that forwards the whole set to the visual manager.
// Your FastArray callbacks already forward per-cell, but this helps with initial replication/snapshots.
void AVoxelChunkNetState::OnRep_Cells()
{
    if (GetLocalRole() == ROLE_Authority) return;

    // Find the client visual world manager
    AVoxelWorldManager* VisualMgr = nullptr;
    for (TActorIterator<AVoxelWorldManager> It(GetWorld()); It; ++It)
    {
        if (IsValid(*It) && It->bClientVisualInstance)
        {
            VisualMgr = *It;
            break;
        }
    }
    if (!VisualMgr) return;

    // Forward batch to manager; it will apply immediately or queue
    VisualMgr->IngestReplicatedCells(ChunkXZ, Cells.Items);
}

void AVoxelChunkNetState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    OnNetStateDestroyed.Broadcast(this);
    Super::EndPlay(EndPlayReason);
}

void AVoxelChunkNetState::ServerApplyCellChange(int32 LocalIndex, uint8 BlockId)
{
    if (HasAuthority())
    {
        // Ensure Owner is set server-side too
        Cells.Owner = this;

        Cells.ServerSetCell(LocalIndex, BlockId);
        // FastArray marking is done inside ServerSetCell via MarkItemDirty(...)
        // If using PushModel, you'd also call MARK_PROPERTY_DIRTY_FROM_NAME here.
    }
}

AVoxelWorldManager* AVoxelChunkNetState::FindLocalVisualManager() const
{
    UWorld* World = GetWorld();
    if (!World) return nullptr;

    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(World, AVoxelWorldManager::StaticClass(), Found);

    for (AActor* A : Found)
    {
        if (AVoxelWorldManager* Mgr = Cast<AVoxelWorldManager>(A))
        {
            // Client visual instance
            if (!Mgr->HasAuthority() && Mgr->bClientVisualInstance)
            {
                return Mgr;
            }
            // Listen server host's local visual manager
            if (Mgr->HasAuthority() && Mgr->bClientVisualInstance)
            {
                return Mgr;
            }
        }
    }
    return nullptr;
}

void AVoxelChunkNetState::ApplyCellOnClient(int32 LocalIndex, uint8 BlockId)
{
    if (IsNetMode(NM_DedicatedServer)) return;

    if (AVoxelWorldManager* VM = FindLocalVisualManager())
    {
        VM->ApplyVisualOpOrQueue(ChunkXZ, LocalIndex, (int32)BlockId);
    }
}

// ------------------------ FModifiedCellArray ------------------------

void FModifiedCellArray::ServerSetCell(int32 LocalIndex, uint8 BlockId)
{
    // find-or-add
    int32 FoundIdx = INDEX_NONE;
    for (int32 i = 0; i < Items.Num(); ++i)
    {
        if (Items[i].LocalIndex == LocalIndex) { FoundIdx = i; break; }
    }

    if (FoundIdx == INDEX_NONE)
    {
        FModifiedCell& NewItem = Items.Emplace_GetRef();
        NewItem.LocalIndex = LocalIndex;
        NewItem.BlockId = BlockId;
        MarkItemDirty(NewItem);
    }
    else
    {
        Items[FoundIdx].BlockId = BlockId;
        MarkItemDirty(Items[FoundIdx]);
    }
}

void FModifiedCellArray::ServerClearCell(int32 LocalIndex)
{
    for (int32 i = 0; i < Items.Num(); ++i)
    {
        if (Items[i].LocalIndex == LocalIndex)
        {
            MarkItemDirty(Items[i]);
            Items.RemoveAtSwap(i);
            MarkArrayDirty();
            break;
        }
    }
}

void FModifiedCellArray::PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 /*FinalSize*/)
{
    if (!Owner) return;
    for (int32 Idx : AddedIndices)
    {
        const FModifiedCell& Cell = Items[Idx];
        Owner->ApplyCellOnClient(Cell.LocalIndex, Cell.BlockId);
    }
}

void FModifiedCellArray::PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 /*FinalSize*/)
{
    if (!Owner) return;
    for (int32 Idx : ChangedIndices)
    {
        const FModifiedCell& Cell = Items[Idx];
        Owner->ApplyCellOnClient(Cell.LocalIndex, Cell.BlockId);
    }
}

void FModifiedCellArray::PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 /*FinalSize*/)
{
    if (!Owner) return;
    // If you ever support deletion, you might choose to set BlockId=Air here.
    for (int32 Idx : RemovedIndices)
    {
        const FModifiedCell& Cell = Items[Idx];
        Owner->ApplyCellOnClient(Cell.LocalIndex, /*Air*/ 0);
    }
}
