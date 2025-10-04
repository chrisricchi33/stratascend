#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "ChunkHelpers.h"
#include "VoxelChunk.h"
#include "VoxelTypes.h"
#include "VoxelWorldManager.generated.h"

class AVoxelChunkActor;
class AVoxelChunkNetState;

UENUM(BlueprintType)
enum class EVoxelWorldSize : uint8
{
    Small  UMETA(DisplayName = "Small"),
    Medium UMETA(DisplayName = "Medium"),
    Large  UMETA(DisplayName = "Large")
};

// Off-thread result: mesh buffers + data.
struct FChunkMeshResult
{
    FChunkKey Key;
    float     BlockSize = 100.f;

    TSharedPtr<FVoxelChunkData> Data;

    TArray<FVector>          V;
    TArray<int32>            I;
    TArray<FVector>          N;
    TArray<FVector2D>        UV;
    TArray<FLinearColor>     C;
    TArray<FProcMeshTangent> T;
};

USTRUCT()
struct FChunkRecord
{
    GENERATED_BODY()

    TSharedPtr<FVoxelChunkData>      Data;
    TWeakObjectPtr<AVoxelChunkActor> Actor;
    bool bDirty = false;
};

// ---- Net structs for replication of edits ----
USTRUCT()
struct FNetModifiedBlock
{
    GENERATED_BODY()
    UPROPERTY() int32 X = 0; UPROPERTY() int32 Y = 0; UPROPERTY() int32 Z = 0;
    UPROPERTY() uint8 Id = 0;
};

USTRUCT()
struct FNetChunkDelta
{
    GENERATED_BODY()
    UPROPERTY() int32 KeyX = 0; UPROPERTY() int32 KeyZ = 0;
    UPROPERTY() TArray<FNetModifiedBlock> Blocks;
};

UCLASS(Blueprintable)
class AVoxelWorldManager : public AActor
{
    GENERATED_BODY()
public:
    AVoxelWorldManager();

    // Rendering
    UPROPERTY(EditAnywhere, Category = "Voxel|Streaming")
    UMaterialInterface* ChunkMaterial = nullptr;

    // Config
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Config", meta = (ExposeOnSpawn = "true", ClampMin = "1.0"))
    float BlockSize = 100.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Config", meta = (ExposeOnSpawn = "true", ClampMin = "0"))
    int32 RenderRadiusChunks = 6;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Config", meta = (ExposeOnSpawn = "true"))
    EVoxelWorldSize WorldSize = EVoxelWorldSize::Small;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Config", meta = (ExposeOnSpawn = "true"))
    int32 WorldSeed = 1337;

    // NEW: world slot name for persistence
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Config", meta = (ExposeOnSpawn = "true"))
    FString WorldName = TEXT("Untitled");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Config", meta = (ExposeOnSpawn = "true", ClampMin = "0.0"))
    float UpdateIntervalSeconds = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Config", meta = (ExposeOnSpawn = "true", ClampMin = "1"))
    int32 MaxConcurrentBackgroundTasks = 8;

    // Track multiple actors (players)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Config", meta = (ExposeOnSpawn = "true"))
    TArray<TObjectPtr<AActor>> TrackedActors;

    // Server vs client visual flags
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Config", meta = (ExposeOnSpawn = "true"))
    bool bClientVisualInstance = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Config", meta = (ExposeOnSpawn = "true"))
    bool bRenderMeshes = true;

    // --- BP helpers ---
    UFUNCTION(BlueprintCallable, Category = "Voxel|Config")
    void AddTrackedActor(AActor* Actor);
    UFUNCTION(BlueprintCallable, Category = "Voxel|Config")
    void RemoveTrackedActor(AActor* Actor);
    UFUNCTION(BlueprintCallable, Category = "Voxel|Config")
    void ClearTrackedActors();

    // Called by AVoxelChunkNetState on clients when cells replicate or change.
    void IngestReplicatedCells(const FIntPoint& ChunkXZ, const TArray<struct FModifiedCell>& Cells);

    // Coordinate helpers
    bool WorldToVoxel_ForEdit(const FVector& World, FChunkKey& OutKey, int32& OutX, int32& OutY, int32& OutZ) const;
    bool WorldToVoxel_Centered(const FVector& World, FChunkKey& OutKey, int32& OutX, int32& OutY, int32& OutZ) const;

    bool ResolveVoxelFromHit(const FVector& WorldHit,
        const FVector& HitNormal,
        bool bForPlacement,
        FChunkKey& OutChunkKey,
        int32& OutLocalIndex,
        FString& OutFail) const;

    UFUNCTION(BlueprintCallable, Category = "Voxel|Edits")
    bool ApplyVisualBlockEdit_ClientBP(FIntPoint ChunkXZ, int32 LocalIndex, int32 NewBlockId);

    bool ApplyBlockEdit_Server(const FChunkKey& ChunkKey,
        int32 LocalIndex,
        int32 NewBlockId,
        TArray<FChunkKey>& OutChunksNeedingRebuild,
        FString& OutFail);

    // Apply a batch of ops to a chunk if loaded; otherwise queue them for when it loads.
    void ApplyOrQueueClientOps(FIntPoint ChunkXZ, const TArray<FBlockEditOp>& Ops);

    // Apply and clear any queued ops for this chunk key (client visual manager).
    void ApplyPendingOpsForChunk(const FChunkKey& Key);

    // Server-side: gather the authoritative modified blocks for a chunk as ops.
    bool GetChunkModifiedOps_Server(const FChunkKey& Key, TArray<FBlockEditOp>& OutOps);

    UFUNCTION(BlueprintCallable, Category = "Voxel|Net")
    void ApplyVisualOpOrQueue(FIntPoint ChunkXZ, int32 LocalIndex, int32 NewBlockId);

    void HandleNetStateDestroyed(class AVoxelChunkNetState* State);

    TSet<FChunkKey> SnapshotRequestedOnce;

    // Hard cap on how many new chunk builds we may enqueue this Tick, regardless of available slots.
// Prevents a huge ring from starting too many jobs at once (which would later finish close together).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Perf", meta = (ClampMin = "1"))
    int32 MaxEnqueuesPerTick = 6;

    // Time budget for draining Completed mesh results (milliseconds).
    // We stop spawning/updating mesh sections as soon as we hit this budget to protect frame time.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Perf", meta = (ClampMin = "0.2"))
    float DrainTimeBudgetMs = 1.5f;

    // Safety cap on total vertices we drain per Tick. Works with the time budget to avoid bursts.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Perf", meta = (ClampMin = "10000"))
    int32 DrainMaxVerticesPerTick = 250000;

    // Optional absolute cap on number of drained items per Tick.
    // Keep a small upper bound even when time/vertex budgets would allow more.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Voxel|Perf", meta = (ClampMin = "1"))
    int32 DrainMaxItemsPerTick = 6;

    static FORCEINLINE bool LocalIndexToXYZ(int32 LI, int32& X, int32& Y, int32& Z)
    {
        if (LI < 0 || LI >= CHUNK_VOLUME) return false;
        XYZFromIndex(LI, X, Y, Z);
        return true;
    }
    static FORCEINLINE int32 XYZToLocalIndex(int32 X, int32 Y, int32 Z)
    {
        return IndexFromXYZ(X, Y, Z);
    }


protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    // Loaded chunk records
    TMap<FChunkKey, FChunkRecord> Loaded;

    // Work queues
    TSet<FChunkKey> Pending;
    TQueue<TSharedPtr<FChunkMeshResult>, EQueueMode::Mpsc> Completed;
    float TimeAcc = 0.f;

    // Pending edits that arrive before a chunk is loaded (client visual)
    TMap<FChunkKey, TArray<FNetModifiedBlock>> PendingNetDeltas;

    // --- Streaming helpers ---
    int32 GetWorldRadiusLimit() const;
    bool  IsWithinWorldLimit(const FChunkKey& Key) const;
    FIntPoint WorldToChunkXZ(const FVector& World) const;
    bool WorldToVoxel(const FVector& World, FChunkKey& OutKey, int32& OutX, int32& OutY, int32& OutZ) const;
    void RecomputeDesiredSet(const FIntPoint& Center, TSet<FChunkKey>& OutDesired) const;
    void GatherCenters(TArray<FIntPoint>& OutCenters) const;

    bool EnsureChunkDataLoaded_ForEdit(const FChunkKey& Key, FChunkRecord*& OutRec);

    // Server-only: replicated net state actors per chunk (no UPROPERTY — FChunkKey is not a USTRUCT)
    TMap<FChunkKey, TWeakObjectPtr<AVoxelChunkNetState>> ChunkNetStates;

    // Server-only helpers
    AVoxelChunkNetState* GetOrCreateNetState_Server(const FChunkKey& Key);
    void DestroyNetState_Server(const FChunkKey& Key);

    void KickBuild(const FChunkKey& Key, TSharedPtr<FVoxelChunkData> Existing);
    void SpawnOrUpdateChunkFromResult(const TSharedPtr<FChunkMeshResult>& Res);
    void UnloadNoLongerNeeded(const TSet<FChunkKey>& Desired);
    void FlushAllDirtyChunks();

public:
    // Allow persistence library to check readiness using Loaded map
    friend class UWorldPersistenceLibrary;
};
