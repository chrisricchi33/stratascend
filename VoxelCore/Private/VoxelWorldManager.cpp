#include "VoxelWorldManager.h"
#include "VoxelChunkActor.h"
#include "VoxelGenerator.h"
#include "VoxelMesher.h"            // FVoxelMesher_Naive
#include "VoxelTypes.h"
#include "ChunkConfig.h"
#include "VoxelSaveSystem.h"
#include "WorldPersistence.h"
#include "VoxelPlayerController.h"
#include "VoxelChunkNetState.h"

#include "Kismet/GameplayStatics.h"
#include "Async/Async.h"
#include "EngineUtils.h"
#include <cstdio> // sscanf

// ---------- ctor / lifecycle ----------

AVoxelWorldManager::AVoxelWorldManager()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = true;
    bReplicates = true; // default; can be overridden in BP
}

void AVoxelWorldManager::BeginPlay()
{
    Super::BeginPlay();

    // On clients, only allow ticking if this instance was explicitly spawned as a "client visual instance".
    if (!HasAuthority() && !bClientVisualInstance)
    {
        SetActorTickEnabled(false);
        SetActorHiddenInGame(true);
        return;
    }

    // Register the visual manager with the local PC (also true on listen server)
    if (bClientVisualInstance)
    {
        if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
        {
            if (AVoxelPlayerController* VPC = Cast<AVoxelPlayerController>(PC))
            {
                VPC->SetClientVisualManager(this);
            }
        }
    }
}

// =======================
// PHASE 6 IMPLEMENTATIONS
// =======================

void AVoxelWorldManager::ApplyOrQueueClientOps(FIntPoint ChunkXZ, const TArray<FBlockEditOp>& Ops)
{
    const FChunkKey Key(ChunkXZ.X, ChunkXZ.Y);
    FChunkRecord* Rec = Loaded.Find(Key);

    // If the chunk's data isn't available yet on this client, queue as net deltas.
    if (!Rec || !Rec->Data.IsValid())
    {
        for (const FBlockEditOp& Op : Ops)
        {
            if (Op.LocalIndex < 0 || Op.LocalIndex >= CHUNK_VOLUME) continue;

            int32 LX = 0, LY = 0, LZ = 0;
            XYZFromIndex(Op.LocalIndex, LX, LY, LZ);

            FNetModifiedBlock B;
            B.X = LX; B.Y = LY; B.Z = LZ;
            B.Id = (uint8)FMath::Clamp(Op.NewBlockId, 0, 255);
            PendingNetDeltas.FindOrAdd(Key).Add(B);
        }
        return;
    }

    // Chunk data is here: apply immediately and rebuild.
    for (const FBlockEditOp& Op : Ops)
    {
        if (Op.LocalIndex < 0 || Op.LocalIndex >= CHUNK_VOLUME) continue;

        int32 LX = 0, LY = 0, LZ = 0;
        XYZFromIndex(Op.LocalIndex, LX, LY, LZ);

        Rec->Data->SetBlockAt(
            LX, LY, LZ,
            static_cast<EBlockId>(FMath::Clamp(Op.NewBlockId, 0, (int32)UINT8_MAX)),
            /*bMarkModified*/true
        );
    }

    Rec->bDirty = true;
    KickBuild(Key, Rec->Data);
}

void AVoxelWorldManager::IngestReplicatedCells(const FIntPoint& ChunkXZ, const TArray<FModifiedCell>& Cells)
{
    const FChunkKey Key(ChunkXZ.X, ChunkXZ.Y);

    // If the chunk is already present locally, apply immediately and rebuild.
    if (FChunkRecord* Rec = Loaded.Find(Key))
    {
        if (Rec->Data.IsValid())
        {
            for (const FModifiedCell& C : Cells)
            {
                int32 LX = 0, LY = 0, LZ = 0;
                XYZFromIndex(C.LocalIndex, LX, LY, LZ);
                Rec->Data->SetBlockAt(LX, LY, LZ, (EBlockId)C.BlockId, /*bMarkModified*/true);
            }
            Rec->bDirty = true;
            KickBuild(Key, Rec->Data);
            return;
        }
    }

    // If the chunk isn't loaded yet here, queue for when it streams in.
    TArray<FNetModifiedBlock>& Queue = PendingNetDeltas.FindOrAdd(Key);
    Queue.Reserve(Queue.Num() + Cells.Num());
    for (const FModifiedCell& C : Cells)
    {
        FNetModifiedBlock B;
        XYZFromIndex(C.LocalIndex, B.X, B.Y, B.Z);
        B.Id = C.BlockId;
        Queue.Add(B);
    }
}



bool AVoxelWorldManager::ApplyVisualBlockEdit_ClientBP(FIntPoint ChunkXZ, int32 LocalIndex, int32 NewBlockId)
{
    const FChunkKey ChunkKeyLocal(ChunkXZ.X, ChunkXZ.Y);
    FChunkRecord* Rec = Loaded.Find(ChunkKeyLocal);
    if (!Rec || !Rec->Data.IsValid())
    {
        // Queue into the same path used by net updates; it’s drained in ApplyPendingOpsForChunk()
        if (LocalIndex < 0 || LocalIndex >= CHUNK_VOLUME) return false;

        int32 LX = 0, LY = 0, LZ = 0;
        XYZFromIndex(LocalIndex, LX, LY, LZ);

        FNetModifiedBlock B;
        B.X = LX; B.Y = LY; B.Z = LZ;
        B.Id = (uint8)FMath::Clamp(NewBlockId, 0, 255);
        PendingNetDeltas.FindOrAdd(ChunkKeyLocal).Add(B);
        return true;
    }

    if (LocalIndex < 0 || LocalIndex >= CHUNK_VOLUME) return false;

    int32 LX = 0, LY = 0, LZ = 0;
    XYZFromIndex(LocalIndex, LX, LY, LZ);

    const int32 ClampedId = FMath::Clamp(NewBlockId, 0, (int32)UINT8_MAX);
    Rec->Data->SetBlockAt(LX, LY, LZ, static_cast<EBlockId>(ClampedId), /*bMarkModified*/true);

    Rec->bDirty = true;
    KickBuild(ChunkKeyLocal, Rec->Data);
    return true;
}

bool AVoxelWorldManager::ResolveVoxelFromHit(const FVector& WorldHit,
    const FVector& HitNormal,
    bool bForPlacement,
    FChunkKey& OutChunkKey,
    int32& OutLocalIndex,
    FString& OutFail) const
{
    const float Bias = 0.08f * BlockSize;
    const FVector N = HitNormal.GetSafeNormal();
    const FVector SampleWS = WorldHit + (bForPlacement ? +Bias : -Bias) * N;

    int32 LX = 0, LY = 0, LZ = 0;
    if (!WorldToVoxel_Centered(SampleWS, OutChunkKey, LX, LY, LZ))
    {
        OutFail = TEXT("Failed to map biased hit to voxel (centered).");
        return false;
    }

    if ((unsigned)LX >= (unsigned)CHUNK_SIZE_X ||
        (unsigned)LY >= (unsigned)CHUNK_SIZE_Y ||
        (unsigned)LZ >= (unsigned)CHUNK_SIZE_Z)
    {
        OutFail = TEXT("Target voxel out of bounds.");
        return false;
    }

    OutLocalIndex = IndexFromXYZ(LX, LY, LZ);
    return true;
}

bool AVoxelWorldManager::EnsureChunkDataLoaded_ForEdit(const FChunkKey& Key, FChunkRecord*& OutRec)
{
    if (FChunkRecord* Found = Loaded.Find(Key))
    {
        if (Found->Data.IsValid()) { OutRec = Found; return true; }
    }

    TSharedPtr<FVoxelChunkData> Data = MakeShared<FVoxelChunkData>(Key);
    {
        FVoxelGenerator Gen(WorldSeed);
        Gen.GenerateBaseChunk(Key, *Data);
    }
    {
        VoxelSaveSystem::LoadDeltaByWorld(WorldName, *Data);
    }

    FChunkRecord NewRec;
    NewRec.Data = Data;
    NewRec.Actor = nullptr;
    NewRec.bDirty = false;

    Loaded.Add(Key, NewRec);
    OutRec = Loaded.Find(Key);
    return OutRec && OutRec->Data.IsValid();
}

bool AVoxelWorldManager::ApplyBlockEdit_Server(const FChunkKey& ChunkKey,
    int32 LocalIndex,
    int32 NewBlockId,
    TArray<FChunkKey>& OutChunksNeedingRebuild,
    FString& OutFail)
{
    if (!HasAuthority()) { OutFail = TEXT("Server only"); return false; }

    FChunkRecord* Rec = Loaded.Find(ChunkKey);
    if (!Rec || !Rec->Data.IsValid())
    {
        if (!EnsureChunkDataLoaded_ForEdit(ChunkKey, Rec))
        {
            OutFail = TEXT("Unable to acquire chunk data for edit.");
            return false;
        }
    }

    if (LocalIndex < 0 || LocalIndex >= CHUNK_VOLUME) { OutFail = TEXT("Bad local index."); return false; }

    int32 LX = 0, LY = 0, LZ = 0;
    XYZFromIndex(LocalIndex, LX, LY, LZ);

    const uint8 ClampedId = (uint8)FMath::Clamp(NewBlockId, 0, (int32)UINT8_MAX);
    const EBlockId OldId = Rec->Data->GetBlockAt(LX, LY, LZ);
    if ((uint8)OldId == ClampedId)
    {
        OutChunksNeedingRebuild.Add(ChunkKey);
        KickBuild(ChunkKey, Rec->Data);
        return true;
    }

    Rec->Data->SetBlockAt(LX, LY, LZ, (EBlockId)ClampedId, /*bMarkModified*/true);
    Rec->bDirty = true;

    OutChunksNeedingRebuild.Add(ChunkKey);
    KickBuild(ChunkKey, Rec->Data);

    // Publish to replicated per-chunk net state
    if (AVoxelChunkNetState* NS = GetOrCreateNetState_Server(ChunkKey))
    {
        NS->ServerApplyCellChange(LocalIndex, ClampedId);
    }

    // Neighbor invalidation for border edits (mesh only)
    if (LX == 0) {
        FChunkKey NK = ChunkKey; NK.X -= 1;
        if (FChunkRecord* NRec = Loaded.Find(NK)) { OutChunksNeedingRebuild.Add(NK); KickBuild(NK, NRec->Data); }
    }
    else if (LX == CHUNK_SIZE_X - 1) {
        FChunkKey NK = ChunkKey; NK.X += 1;
        if (FChunkRecord* NRec = Loaded.Find(NK)) { OutChunksNeedingRebuild.Add(NK); KickBuild(NK, NRec->Data); }
    }
    if (LZ == 0) {
        FChunkKey NK = ChunkKey; NK.Z -= 1;
        if (FChunkRecord* NRec = Loaded.Find(NK)) { OutChunksNeedingRebuild.Add(NK); KickBuild(NK, NRec->Data); }
    }
    else if (LZ == CHUNK_SIZE_Z - 1) {
        FChunkKey NK = ChunkKey; NK.Z += 1;
        if (FChunkRecord* NRec = Loaded.Find(NK)) { OutChunksNeedingRebuild.Add(NK); KickBuild(NK, NRec->Data); }
    }
    return true;
}

void AVoxelWorldManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    FlushAllDirtyChunks();
    Super::EndPlay(EndPlayReason);
}

// ---------- world limits / mapping ----------

int32 AVoxelWorldManager::GetWorldRadiusLimit() const
{
    switch (WorldSize)
    {
    case EVoxelWorldSize::Small:  return 16;
    case EVoxelWorldSize::Medium: return 64;
    case EVoxelWorldSize::Large:  return 256;
    default: return 16;
    }
}

bool AVoxelWorldManager::IsWithinWorldLimit(const FChunkKey& Key) const
{
    const int32 R = GetWorldRadiusLimit();
    return FMath::Abs(Key.X) <= R && FMath::Abs(Key.Z) <= R;
}

FIntPoint AVoxelWorldManager::WorldToChunkXZ(const FVector& W) const
{
    const double CSX = (double)CHUNK_SIZE_X * BlockSize;
    const double CSZ = (double)CHUNK_SIZE_Z * BlockSize;
    const int32 Cx = FMath::FloorToInt(W.X / CSX);
    const int32 Cz = FMath::FloorToInt(W.Y / CSZ); // chunk Z -> world Y
    return FIntPoint(Cx, Cz);
}

// Robust world->voxel using a global grid (handles negatives).
bool AVoxelWorldManager::WorldToVoxel(const FVector& W, FChunkKey& OutKey, int32& x, int32& y, int32& z) const
{
    const double invBS = 1.0 / (double)BlockSize;
    const int32 GX = FMath::FloorToInt(W.X * invBS); // world X -> voxel X
    const int32 GZ = FMath::FloorToInt(W.Y * invBS); // world Y -> voxel Z
    const int32 GY = FMath::FloorToInt(W.Z * invBS); // world Z -> voxel Y

    const int32 CX = FMath::FloorToInt((double)GX / (double)CHUNK_SIZE_X);
    const int32 CZ = FMath::FloorToInt((double)GZ / (double)CHUNK_SIZE_Z);

    const int32 LX = GX - CX * CHUNK_SIZE_X;
    const int32 LZ = GZ - CZ * CHUNK_SIZE_Z;
    const int32 LY = FMath::Clamp(GY, 0, CHUNK_SIZE_Y - 1);

    OutKey = FChunkKey(CX, CZ);
    x = FMath::Clamp(LX, 0, CHUNK_SIZE_X - 1);
    z = FMath::Clamp(LZ, 0, CHUNK_SIZE_Z - 1);
    y = LY;
    return true;
}

bool AVoxelWorldManager::WorldToVoxel_Centered(const FVector& World,
    FChunkKey& OutKey, int32& OutX, int32& OutY, int32& OutZ) const
{
    const double BS = (double)BlockSize;

    const int32 GX = FMath::FloorToInt(World.X / BS + 0.5);
    const int32 GZ = FMath::FloorToInt(World.Y / BS + 0.5);
    const int32 GY = FMath::FloorToInt(World.Z / BS + 0.5);

    const int32 CX = FMath::FloorToInt((double)GX / (double)CHUNK_SIZE_X);
    const int32 CZ = FMath::FloorToInt((double)GZ / (double)CHUNK_SIZE_Z);

    OutX = GX - CX * CHUNK_SIZE_X;
    OutZ = GZ - CZ * CHUNK_SIZE_Z;
    OutY = FMath::Clamp(GY, 0, CHUNK_SIZE_Y - 1);
    OutKey = FChunkKey(CX, CZ);
    return true;
}

bool AVoxelWorldManager::WorldToVoxel_ForEdit(const FVector& World,
    FChunkKey& OutKey, int32& OutX, int32& OutY, int32& OutZ) const
{
    return WorldToVoxel(World, OutKey, OutX, OutY, OutZ);
}

// ---------- desired sets / centers ----------

void AVoxelWorldManager::RecomputeDesiredSet(const FIntPoint& Center, TSet<FChunkKey>& OutDesired) const
{
    OutDesired.Reset();
    const int32 R = RenderRadiusChunks;
    for (int32 dz = -R; dz <= R; ++dz)
    {
        for (int32 dx = -R; dx <= R; ++dx)
        {
            const FChunkKey Key(Center.X + dx, Center.Y + dz);
            if (IsWithinWorldLimit(Key)) OutDesired.Add(Key);
        }
    }
}

void AVoxelWorldManager::GatherCenters(TArray<FIntPoint>& OutCenters) const
{
    OutCenters.Reset();

    for (AActor* A : TrackedActors)
    {
        if (IsValid(A))
        {
            OutCenters.Add(WorldToChunkXZ(A->GetActorLocation()));
        }
    }

    if (OutCenters.Num() == 0)
    {
        if (AActor* Pawn0 = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
        {
            OutCenters.Add(WorldToChunkXZ(Pawn0->GetActorLocation()));
        }
    }
}

// ---------- build / spawn / unload ----------

void AVoxelWorldManager::KickBuild(const FChunkKey& Key, TSharedPtr<FVoxelChunkData> Existing)
{
    if (Pending.Contains(Key)) return;
    Pending.Add(Key);

    const int32 Seed = WorldSeed;
    const float BS = BlockSize;
    const FString WName = WorldName;

    Async(EAsyncExecution::ThreadPool, [this, Key, Existing, Seed, BS, WName]()
        {
            TSharedPtr<FVoxelChunkData> Data = Existing;
            if (!Data.IsValid())
            {
                Data = MakeShared<FVoxelChunkData>(Key);
                FVoxelGenerator Gen(Seed);
                Gen.GenerateBaseChunk(Key, *Data);

                VoxelSaveSystem::LoadDeltaByWorld(WName, *Data);
            }

            TSharedPtr<FChunkMeshResult> R = MakeShared<FChunkMeshResult>();
            R->Key = Key;
            R->BlockSize = BS;
            R->Data = Data;

            FVoxelMesher_Naive::BuildMesh(*Data, BS, R->V, R->I, R->N, R->UV, R->C, R->T);

            Completed.Enqueue(R);
        });
}

void AVoxelWorldManager::SpawnOrUpdateChunkFromResult(const TSharedPtr<FChunkMeshResult>& Res)
{
    if (!Res) return;
    if (!IsWithinWorldLimit(Res->Key)) return;

    FChunkRecord& Rec = Loaded.FindOrAdd(Res->Key);
    if (!Rec.Data.IsValid())
    {
        Rec.Data = Res->Data.IsValid() ? Res->Data : MakeShared<FVoxelChunkData>(Res->Key);
    }

    // Apply any pending replicated edits that arrived before this chunk finished loading
    bool bAppliedPending = false;
    if (PendingNetDeltas.Contains(Res->Key))
    {
        TArray<FNetModifiedBlock>& Arr = PendingNetDeltas.FindChecked(Res->Key);
        for (const FNetModifiedBlock& B : Arr)
        {
            Rec.Data->SetBlockAt(B.X, B.Y, B.Z, (EBlockId)B.Id, /*bMarkModified*/true);
        }
        Arr.Reset();
        PendingNetDeltas.Remove(Res->Key);
        bAppliedPending = true;
    }

    // Spawn or fetch the visual actor
    AVoxelChunkActor* Actor = Rec.Actor.Get();
    if (!Actor || !IsValid(Actor))
    {
        const FVector Origin(
            (double)Res->Key.X * CHUNK_SIZE_X * Res->BlockSize,
            (double)Res->Key.Z * CHUNK_SIZE_Z * Res->BlockSize,
            0.0
        );
        FActorSpawnParameters SP;
        SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Actor = GetWorld()->SpawnActor<AVoxelChunkActor>(Origin, FRotator::ZeroRotator, SP);
        if (!Actor) return;
        Actor->BlockSize = Res->BlockSize;
        Actor->SetRenderMeshes(bRenderMeshes);
        Rec.Actor = Actor;
    }
    else
    {
        Actor->SetRenderMeshes(bRenderMeshes);
    }

    // If we applied pending deltas, the Res* buffers are stale relative to Rec.Data.
    // Schedule a fresh build and skip drawing the stale mesh.
    if (bAppliedPending)
    {
        Rec.bDirty = false;               // we'll rebuild immediately
        KickBuild(Res->Key, Rec.Data);
    }
    else
    {
        // No pending edits: draw the buffers we just built
        Actor->BuildFromBuffers(Res->V, Res->I, Res->N, Res->UV, Res->C, Res->T, ChunkMaterial);
        Rec.bDirty = false;
    }

    // === Server: ensure there is a net-state actor for this chunk ===
    if (HasAuthority() && !bClientVisualInstance)
    {
        if (AVoxelChunkNetState* NS = GetOrCreateNetState_Server(Res->Key))
        {
            const FVector Origin(
                (double)Res->Key.X * CHUNK_SIZE_X * Res->BlockSize,
                (double)Res->Key.Z * CHUNK_SIZE_Z * Res->BlockSize,
                0.0
            );
            NS->SetActorLocation(Origin);
        }
    }

    // === Client visual: defensive — apply any super-late ops
    if (bClientVisualInstance)
    {
        ApplyPendingOpsForChunk(Res->Key);

        // NEW: ask the server for an authoritative snapshot of THIS chunk once
        if (!SnapshotRequestedOnce.Contains(Res->Key))
        {
            SnapshotRequestedOnce.Add(Res->Key);

            if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
            {
                if (AVoxelPlayerController* VPC = Cast<AVoxelPlayerController>(PC))
                {
                    // Radius 0 = just this chunk; bump to 1 if you want neighbors too
                    VPC->Server_RequestChunkSnapshotsForArea(FIntPoint(Res->Key.X, Res->Key.Z), /*Radius=*/0);
                }
            }
        }
    }
}

void AVoxelWorldManager::UnloadNoLongerNeeded(const TSet<FChunkKey>& Desired)
{
    TArray<FChunkKey> ToUnload;
    ToUnload.Reserve(Loaded.Num());

    auto WithinUnloadPad = [this](const FChunkKey& TestKey)->bool
        {
            TArray<FIntPoint> Centers;
            GatherCenters(Centers);
            if (Centers.Num() == 0) return false;

            const int32 Pad = 1;
            for (const FIntPoint& C : Centers)
            {
                if (FMath::Abs(TestKey.X - C.X) <= (RenderRadiusChunks + Pad) &&
                    FMath::Abs(TestKey.Z - C.Y) <= (RenderRadiusChunks + Pad))
                {
                    return true;
                }
            }
            return false;
        };

    for (auto& Pair : Loaded)
    {
        const FChunkKey& ThisKey = Pair.Key;
        FChunkRecord& Rec = Pair.Value;

        if (!Desired.Contains(ThisKey) && !WithinUnloadPad(ThisKey))
        {
            // Despawn actor if present
            if (AVoxelChunkActor* A = Rec.Actor.Get())
            {
                A->Destroy();
                Rec.Actor = nullptr;
            }

            // Off-thread save of modified blocks (authoritative only)
            if (HasAuthority() && Rec.Data.IsValid() && Rec.Data->ModifiedBlocks.Num() > 0)
            {
                const FString WorldNameCopy = WorldName; // capture by value
                // Copy the minimal data we need to avoid touching Rec.Data later
                TSharedPtr<FVoxelChunkData> DataCopy = MakeShared<FVoxelChunkData>(*Rec.Data.Get());

                Async(EAsyncExecution::ThreadPool, [WorldNameCopy, DataCopy]()
                    {
                        VoxelSaveSystem::SaveDeltaByWorld(WorldNameCopy, *DataCopy.Get());
                    });
            }

            ToUnload.Add(ThisKey);

            // Tear down net-state on server
            if (HasAuthority() && !bClientVisualInstance)
            {
                DestroyNetState_Server(ThisKey);
            }
        }
    }

    for (const FChunkKey& K : ToUnload)
    {
        Loaded.Remove(K);
        Pending.Remove(K);
    }
}


void AVoxelWorldManager::ApplyPendingOpsForChunk(const FChunkKey& Key)
{
    if (!PendingNetDeltas.Contains(Key)) return;

    FChunkRecord* Rec = Loaded.Find(Key);
    if (!Rec || !Rec->Data.IsValid()) return;

    TArray<FNetModifiedBlock>& Ops = PendingNetDeltas.FindChecked(Key);
    for (const FNetModifiedBlock& B : Ops)
    {
        Rec->Data->SetBlockAt(B.X, B.Y, B.Z, (EBlockId)B.Id, true);
    }
    Ops.Reset();
    PendingNetDeltas.Remove(Key);

    Rec->bDirty = true;
    KickBuild(Key, Rec->Data);
}

void AVoxelWorldManager::ApplyVisualOpOrQueue(FIntPoint ChunkXZ, int32 LocalIndex, int32 NewBlockId)
{
    const FChunkKey ChunkKeyLocal(ChunkXZ.X, ChunkXZ.Y);

    int32 LX = 0, LY = 0, LZ = 0;
    XYZFromIndex(LocalIndex, LX, LY, LZ);

    FChunkRecord* Rec = Loaded.Find(ChunkKeyLocal);
    if (!Rec || !Rec->Data.IsValid())
    {
        FNetModifiedBlock B; B.X = LX; B.Y = LY; B.Z = LZ; B.Id = (uint8)FMath::Clamp(NewBlockId, 0, 255);
        PendingNetDeltas.FindOrAdd(ChunkKeyLocal).Add(B);
        return;
    }

    Rec->Data->SetBlockAt(LX, LY, LZ, static_cast<EBlockId>(FMath::Clamp(NewBlockId, 0, 255)), true);
    Rec->bDirty = true;
    KickBuild(ChunkKeyLocal, Rec->Data);
}

bool AVoxelWorldManager::GetChunkModifiedOps_Server(const FChunkKey& Key, TArray<FBlockEditOp>& OutOps)
{
    FChunkRecord* Rec = Loaded.Find(Key);
    if (!Rec || !Rec->Data.IsValid())
    {
        if (!EnsureChunkDataLoaded_ForEdit(Key, Rec))
            return false;
    }

    OutOps.Reset();
    OutOps.Reserve(Rec->Data->ModifiedBlocks.Num());
    for (const auto& Pair : Rec->Data->ModifiedBlocks)  // int32 LocalIndex -> uint8 (EBlockId)
    {
        FBlockEditOp Op;
        Op.ChunkXZ = FIntPoint(Key.X, Key.Z);
        Op.LocalIndex = Pair.Key;
        Op.NewBlockId = (int32)Pair.Value;
        OutOps.Add(Op);
    }
    return true;
}

// ---------- tick / streaming ----------

void AVoxelWorldManager::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // ---------------------------
    // Drain: time/vertex-budgeted
    // ---------------------------
    {
        const double StartSec = FPlatformTime::Seconds();
        const double BudgetSec = DrainTimeBudgetMs / 1000.0;

        int32   DrainedItems = 0;
        int32   DrainedVertices = 0;
        TSharedPtr<FChunkMeshResult> Res;

        // Pull completed results while within time, vertex, and item budgets.
        while (DrainedItems < DrainMaxItemsPerTick && Completed.Dequeue(Res))
        {
            // Remove from 'Pending' set to free a background slot
            Pending.Remove(Res->Key);

            // Spawn/update visual actor for this chunk
            SpawnOrUpdateChunkFromResult(Res);
            ++DrainedItems;

            // Track vertex budget (guard against pathological meshes)
            DrainedVertices += Res->V.Num();
            if (DrainedVertices >= DrainMaxVerticesPerTick)
            {
                break; // hit vertex budget
            }

            // Rebuild if the record became dirty during async work
            if (FChunkRecord* Rec = Loaded.Find(Res->Key))
            {
                if (Rec->bDirty && !Pending.Contains(Res->Key))
                {
                    Rec->bDirty = false;
                    KickBuild(Res->Key, Rec->Data);
                }
            }

            // Time budget check last (cheapest to evaluate at end of body)
            const double NowSec = FPlatformTime::Seconds();
            if ((NowSec - StartSec) >= BudgetSec)
            {
                break; // hit time budget
            }
        }
    }

    // -----------------------------
    // Update centers at fixed cadence
    // -----------------------------
    TimeAcc += DeltaSeconds;
    if (TimeAcc < UpdateIntervalSeconds) return;
    TimeAcc = 0.f;

    // === build desired set around ALL tracked actors ===
    TArray<FIntPoint> Centers;
    GatherCenters(Centers);
    if (Centers.Num() == 0) return;

    // Union of desired chunks
    TSet<FChunkKey> Desired;
    for (const FIntPoint& C : Centers)
    {
        TSet<FChunkKey> Tmp;
        RecomputeDesiredSet(C, Tmp);
        Desired.Append(Tmp);
    }

    // Order by min manhattan distance to ANY center (nearest-first work scheduling)
    TArray<FChunkKey> DesiredOrdered = Desired.Array();
    auto DistToCenters = [&Centers](const FChunkKey& K) -> int32
        {
            int32 Best = MAX_int32;
            for (const FIntPoint& C : Centers)
            {
                const int32 d = FMath::Abs(K.X - C.X) + FMath::Abs(K.Z - C.Y);
                Best = FMath::Min(Best, d);
            }
            return Best;
        };
    DesiredOrdered.Sort([&](const FChunkKey& A, const FChunkKey& B)
        {
            return DistToCenters(A) < DistToCenters(B);
        });

    // Unload anything not desired by ANY player (with hysteresis)
    UnloadNoLongerNeeded(Desired);

    // ------------------------------------------
    // Enqueue: cap both concurrency and per-tick
    // ------------------------------------------
    int32 Slots = FMath::Max(0, MaxConcurrentBackgroundTasks - Pending.Num());
    int32 EnqueueBudget = FMath::Min(Slots, MaxEnqueuesPerTick);

    for (const FChunkKey& K : DesiredOrdered)
    {
        if (EnqueueBudget <= 0) break;

        const FChunkRecord* Rec = Loaded.Find(K);
        const bool bHasActor = Rec && Rec->Actor.IsValid();
        if (bHasActor) continue;           // already spawned
        if (Pending.Contains(K)) continue; // already building

        TSharedPtr<FVoxelChunkData> Existing = Rec ? Rec->Data : nullptr;
        KickBuild(K, Existing);
        --EnqueueBudget;
    }
}


// ---------- tracked actors mgmt & persistence (definitions restored) ----------

void AVoxelWorldManager::FlushAllDirtyChunks()
{
    if (!HasAuthority()) return;

    for (auto& Pair : Loaded)
    {
        FChunkRecord& Rec = Pair.Value;
        if (Rec.Data.IsValid() && Rec.Data->ModifiedBlocks.Num() > 0)
        {
            VoxelSaveSystem::SaveDeltaByWorld(WorldName, *Rec.Data);
            Rec.bDirty = false;
        }
    }
}

void AVoxelWorldManager::AddTrackedActor(AActor* Actor)
{
    if (!IsValid(Actor)) return;
    TrackedActors.AddUnique(Actor);
}

void AVoxelWorldManager::RemoveTrackedActor(AActor* Actor)
{
    TrackedActors.Remove(Actor);
}

void AVoxelWorldManager::ClearTrackedActors()
{
    TrackedActors.Reset();
}

// ---------- replicated chunk net-state helpers (server only) ----------

AVoxelChunkNetState* AVoxelWorldManager::GetOrCreateNetState_Server(const FChunkKey& Key)
{
    if (!HasAuthority()) return nullptr;

    if (TWeakObjectPtr<AVoxelChunkNetState>* Found = ChunkNetStates.Find(Key))
    {
        if (Found->IsValid()) return Found->Get();
    }

    FActorSpawnParameters SP;
    SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AVoxelChunkNetState* NS = GetWorld()->SpawnActor<AVoxelChunkNetState>(FVector::ZeroVector, FRotator::ZeroRotator, SP);
    if (!NS) return nullptr;

    NS->ChunkXZ = FIntPoint(Key.X, Key.Z);
    NS->Cells.Owner = NS;
    NS->OnNetStateDestroyed.AddUObject(this, &AVoxelWorldManager::HandleNetStateDestroyed);
    ChunkNetStates.Add(Key, NS);
    return NS;
}

void AVoxelWorldManager::DestroyNetState_Server(const FChunkKey& Key)
{
    if (!HasAuthority()) return;

    if (TWeakObjectPtr<AVoxelChunkNetState>* Found = ChunkNetStates.Find(Key))
    {
        if (Found->IsValid())
        {
            Found->Get()->Destroy();
        }
        ChunkNetStates.Remove(Key);
    }
}

void AVoxelWorldManager::HandleNetStateDestroyed(AVoxelChunkNetState* State)
{
    if (!State) return;
    for (auto It = ChunkNetStates.CreateIterator(); It; ++It)
    {
        if (It->Value.Get() == State)
        {
            It.RemoveCurrent();
            break;
        }
    }
}
