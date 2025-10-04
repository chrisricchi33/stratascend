#include "VoxelPlayerController.h"            // MUST be first
#include "VoxelWorldManager.h"
#include "Kismet/GameplayStatics.h"
#include "ChunkConfig.h"
#include "Engine/World.h"

// -----------------------------------------------------------------------------
// Helper: find the authoritative world manager on the server
// -----------------------------------------------------------------------------
AVoxelWorldManager* AVoxelPlayerController::GetAuthoritativeWorldManager() const
{
    UWorld* World = GetWorld();
    if (!World) return nullptr;

    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(World, AVoxelWorldManager::StaticClass(), Found);

    for (AActor* A : Found)
    {
        if (AVoxelWorldManager* Mgr = Cast<AVoxelWorldManager>(A))
        {
            if (Mgr->HasAuthority() && !Mgr->bClientVisualInstance)
            {
                return Mgr;
            }
        }
    }
    return nullptr;
}

// -----------------------------------------------------------------------------
// Client → Server: request an edit (place/remove) [single-op path]
// -----------------------------------------------------------------------------
void AVoxelPlayerController::Server_RequestBlockEdit_Implementation(const FBlockEditRequest& Req)
{
    APawn* P = GetPawn();
    if (!P) return;

    // Basic reach/sanity
    const float MaxReach = FMath::Clamp(Req.ClaimedReach, 150.f, 800.f);
    if (FVector::Dist(P->GetActorLocation(), Req.WorldHitLocation) > MaxReach)
        return;

    AVoxelWorldManager* ServerMgr = GetAuthoritativeWorldManager();
    if (!ServerMgr) return;

    // World hit → (chunk, localIndex). For placement, step into the adjacent cell along normal.
    FChunkKey Key; int32 LocalIndex = 0; FString Fail;
    const bool bForPlacement = (Req.Action == EVoxelEditAction::Place);
    if (!ServerMgr->ResolveVoxelFromHit(Req.WorldHitLocation, Req.HitNormal, bForPlacement, Key, LocalIndex, Fail))
        return;

    // Apply on server (authoritative).
    TArray<FChunkKey> Dummy;
    const int32 NewId = (Req.Action == EVoxelEditAction::Place) ? Req.NewBlockId : 0;
    FString Reason;
    if (!ServerMgr->ApplyBlockEdit_Server(Key, LocalIndex, NewId, Dummy, Reason))
        return;

    // Broadcast to ALL clients (including owner)
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        if (AVoxelPlayerController* PC = Cast<AVoxelPlayerController>(It->Get()))
        {
            FBlockEditOp Op;
            Op.ChunkXZ = FIntPoint(Key.X, Key.Z);
            Op.LocalIndex = LocalIndex;
            Op.NewBlockId = NewId;
            PC->Client_ApplyRemoteBlockEdit(Op);
        }
    }
}

// -----------------------------------------------------------------------------
// Server → Client: apply one remote block edit to client-visual world
// -----------------------------------------------------------------------------
void AVoxelPlayerController::Client_ApplyRemoteBlockEdit_Implementation(const FBlockEditOp& Op)
{
    if (!ClientVisualManager)
    {
        // Fallback: find one if BP didn't wire it yet
        TArray<AActor*> Found;
        UGameplayStatics::GetAllActorsOfClass(GetWorld(), AVoxelWorldManager::StaticClass(), Found);
        for (AActor* A : Found)
        {
            if (AVoxelWorldManager* M = Cast<AVoxelWorldManager>(A))
            {
                if (M->bClientVisualInstance) { ClientVisualManager = M; break; }
            }
        }
    }
    if (!ClientVisualManager) return;

    // Apply the single-op delta to the client-visual world
    ClientVisualManager->ApplyVisualOpOrQueue(Op.ChunkXZ, Op.LocalIndex, Op.NewBlockId);

    // --- NEW: verify-after-edit snapshot (throttled per chunk) ---
    // This ensures that after rapid bursts, we reconcile to server truth.
    if (IsLocalController()) // only the owning client asks; peers don't need to
    {
        const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
        const float* NextAllowed = NextSnapshotAllowedTime.Find(Op.ChunkXZ);
        if (!NextAllowed || Now >= *NextAllowed)
        {
            // Ask the server for the authoritative modified set for this chunk
            Server_RequestChunkSnapshot(Op.ChunkXZ);

            // Cooldown ~150 ms per chunk to avoid spamming under bursts
            NextSnapshotAllowedTime.Add(Op.ChunkXZ, Now + 0.15f);
        }
    }
}


// -----------------------------------------------------------------------------
// One-chunk snapshot on request (used by initial/area sync)
// -----------------------------------------------------------------------------
void AVoxelPlayerController::Server_RequestChunkSnapshot_Implementation(FIntPoint ChunkXZ)
{
    AVoxelWorldManager* ServerMgr = GetAuthoritativeWorldManager();
    if (!ServerMgr) return;

    TArray<FBlockEditOp> Ops;
    if (ServerMgr->GetChunkModifiedOps_Server(FChunkKey(ChunkXZ.X, ChunkXZ.Y), Ops))
    {
        Client_ReceiveChunkSnapshot(ChunkXZ, Ops);
    }
}

static AVoxelWorldManager* FindLocalVisualManager(UWorld* W)
{
    TArray<AActor*> Found;
    UGameplayStatics::GetAllActorsOfClass(W, AVoxelWorldManager::StaticClass(), Found);
    for (AActor* A : Found)
    {
        if (AVoxelWorldManager* M = Cast<AVoxelWorldManager>(A))
        {
            if (M->bClientVisualInstance) return M; // includes listen server local client
        }
    }
    return nullptr;
}

void AVoxelPlayerController::Client_ReceiveChunkSnapshot_Implementation(FIntPoint ChunkXZ, const TArray<FBlockEditOp>& Ops)
{
    if (!ClientVisualManager)
    {
        if (UWorld* W = GetWorld())
        {
            ClientVisualManager = FindLocalVisualManager(W);
        }
    }
    if (!ClientVisualManager) return;
    ClientVisualManager->ApplyOrQueueClientOps(ChunkXZ, Ops);
}

// -----------------------------------------------------------------------------
// Initial/area snapshot helpers (late-join correctness)
// -----------------------------------------------------------------------------
void AVoxelPlayerController::Server_RequestInitialChunkSnapshots_Implementation()
{
    AVoxelWorldManager* Manager = nullptr;
    {
        TArray<AActor*> Found;
        UGameplayStatics::GetAllActorsOfClass(GetWorld(), AVoxelWorldManager::StaticClass(), Found);
        for (AActor* A : Found)
        {
            if (AVoxelWorldManager* M = Cast<AVoxelWorldManager>(A))
            {
                if (M->HasAuthority() && !M->bClientVisualInstance) { Manager = M; break; }
            }
        }
    }
    if (!Manager) return;

    APawn* P = GetPawn();
    if (!P) return;

    // Compute center chunk without private helpers
    const FVector L = P->GetActorLocation();
    const double BS = (double)Manager->BlockSize;
    const int32 Cx = FMath::FloorToInt(L.X / (CHUNK_SIZE_X * BS));
    const int32 Cz = FMath::FloorToInt(L.Y / (CHUNK_SIZE_Z * BS));

    const FIntPoint Center(Cx, Cz);
    const int32 Radius = Manager->RenderRadiusChunks;

    Server_RequestChunkSnapshotsForArea(Center, Radius);
}

void AVoxelPlayerController::RequestInitialChunkSnapshotsBP()
{
    Server_RequestInitialChunkSnapshots();
}

void AVoxelPlayerController::RequestChunkSnapshotsForAreaBP(FIntPoint CenterChunk, int32 Radius)
{
    Server_RequestChunkSnapshotsForArea(CenterChunk, Radius);
}

void AVoxelPlayerController::Server_RequestChunkSnapshotsForArea_Implementation(FIntPoint CenterChunk, int32 Radius)
{
    AVoxelWorldManager* Manager = nullptr;
    {
        TArray<AActor*> Found;
        UGameplayStatics::GetAllActorsOfClass(GetWorld(), AVoxelWorldManager::StaticClass(), Found);
        for (AActor* A : Found)
        {
            if (AVoxelWorldManager* M = Cast<AVoxelWorldManager>(A))
            {
                if (M->HasAuthority() && !M->bClientVisualInstance) { Manager = M; break; }
            }
        }
    }
    if (!Manager) return;

    for (int32 dz = -Radius; dz <= Radius; ++dz)
    {
        for (int32 dx = -Radius; dx <= Radius; ++dx)
        {
            const FChunkKey Key(CenterChunk.X + dx, CenterChunk.Y + dz);
            TArray<FBlockEditOp> Ops;
            if (Manager->GetChunkModifiedOps_Server(Key, Ops) && Ops.Num() > 0)
            {
                Client_ReceiveChunkSnapshot(FIntPoint(Key.X, Key.Z), Ops);
            }
        }
    }
}
