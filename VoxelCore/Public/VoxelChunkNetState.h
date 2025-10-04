#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "VoxelChunkNetState.generated.h"

// Forward decl
class AVoxelWorldManager;

USTRUCT()
struct FModifiedCell : public FFastArraySerializerItem
{
    GENERATED_BODY()

    UPROPERTY()
    int32 LocalIndex = 0;

    UPROPERTY()
    uint8 BlockId = 0;
};

USTRUCT()
struct FModifiedCellArray : public FFastArraySerializer
{
    GENERATED_BODY()

    UPROPERTY()
    TArray<FModifiedCell> Items;

    // Not replicated; set on both server and client in BeginPlay so callbacks can find the owner.
    AVoxelChunkNetState* Owner = nullptr;

    // ---- Server-side mutation helpers ----
    void ServerSetCell(int32 LocalIndex, uint8 BlockId);
    void ServerClearCell(int32 LocalIndex);

    // ---- FastArray replication callbacks (client side) ----
    void PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize);
    void PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize);
    void PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize);

    // ---- NetDelta serialization ----
    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FModifiedCell, FModifiedCellArray>(Items, DeltaParms, *this);
    }
};

template<>
struct TStructOpsTypeTraits<FModifiedCellArray> : public TStructOpsTypeTraitsBase2<FModifiedCellArray>
{
    enum
    {
        WithNetDeltaSerializer = true,
    };
};

// Delegate used by the manager to clean up its map on destruction
DECLARE_MULTICAST_DELEGATE_OneParam(FOnNetStateDestroyed, AVoxelChunkNetState* /*State*/);

UCLASS()
class VOXELCORE_API AVoxelChunkNetState : public AActor
{
    GENERATED_BODY()

public:
    AVoxelChunkNetState();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // Chunk coordinate (X,Z in chunk space)
    UPROPERTY(Replicated)
    FIntPoint ChunkXZ = FIntPoint::ZeroValue;

    // Replicated fast array of modified cells for this chunk
    UPROPERTY(ReplicatedUsing = OnRep_Cells)
    FModifiedCellArray Cells;

    // Broadcast when this actor is destroyed (manager listens to remove stale entries)
    FOnNetStateDestroyed OnNetStateDestroyed;

    // Called by server-side world manager to upsert a single cell change
    void ServerApplyCellChange(int32 LocalIndex, uint8 BlockId);

    // Client helper: apply a single cell to the local visual manager (used by FastArray callbacks)
    void ApplyCellOnClient(int32 LocalIndex, uint8 BlockId);

    // Optional batch path (also useful for initial replication)
    UFUNCTION()
    void OnRep_Cells();

protected:
    AVoxelWorldManager* FindLocalVisualManager() const;

    // Replication boilerplate
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
