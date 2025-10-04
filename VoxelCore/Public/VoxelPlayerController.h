#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "VoxelTypes.h"

#include "VoxelPlayerController.generated.h"

class AVoxelWorldManager;

/**
 * C++ base for BP_FirstPersonPlayerController.
 * - Client sends Server_RequestBlockEdit(FBlockEditRequest)  [single-op path]
 * - Server resolves & applies on the authoritative world manager
 * - Server broadcasts a compact op to all clients
 * - Client applies op to its client-visual world manager
 */
UCLASS(Blueprintable)
class VOXELCORE_API AVoxelPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	/** Client → Server request (single edit) */
	UFUNCTION(Server, Reliable)
	void Server_RequestBlockEdit(const FBlockEditRequest& Req);
	void Server_RequestBlockEdit_Implementation(const FBlockEditRequest& Req);

	/** Server → Client broadcast (single op to peers) */
	UFUNCTION(Client, Reliable)
	void Client_ApplyRemoteBlockEdit(const FBlockEditOp& Op);
	void Client_ApplyRemoteBlockEdit_Implementation(const FBlockEditOp& Op);

	/** BP sets this to the client-visual world manager it spawns/owns. */
	UPROPERTY(BlueprintReadWrite, Category = "Voxel")
	AVoxelWorldManager* ClientVisualManager = nullptr;

	/** BP helper to set the client-visual manager. */
	UFUNCTION(BlueprintCallable, Category = "Voxel")
	void SetClientVisualManager(AVoxelWorldManager* Mgr) { ClientVisualManager = Mgr; }

	/** Ask server for one specific chunk's snapshot (authoritative modified cells) */
	UFUNCTION(Server, Reliable)
	void Server_RequestChunkSnapshot(FIntPoint ChunkXZ);
	void Server_RequestChunkSnapshot_Implementation(FIntPoint ChunkXZ);

	// ---- BP-friendly wrappers (late-join/area snapshot) ----
	UFUNCTION(BlueprintCallable, Category = "Voxels|Net")
	void RequestInitialChunkSnapshotsBP();

	UFUNCTION(BlueprintCallable, Category = "Voxels|Net")
	void RequestChunkSnapshotsForAreaBP(FIntPoint CenterChunk, int32 Radius);

	/** Owning client → server: request initial snapshots around pawn */
	UFUNCTION(Server, Reliable)
	void Server_RequestInitialChunkSnapshots();

	/** Server helper: request snapshots for a specific area (center/radius in chunk space) */
	UFUNCTION(Server, Reliable)
	void Server_RequestChunkSnapshotsForArea(FIntPoint CenterChunk, int32 Radius);

	/** Server → client: push a snapshot (list of modified ops) for a chunk */
	UFUNCTION(Client, Reliable)
	void Client_ReceiveChunkSnapshot(FIntPoint ChunkXZ, const TArray<FBlockEditOp>& Ops);
	void Client_ReceiveChunkSnapshot_Implementation(FIntPoint ChunkXZ, const TArray<FBlockEditOp>& Ops);

	// Throttle per-chunk snapshot requests after receiving edits (client-side safety net)
	UPROPERTY()
	TMap<FIntPoint, float> NextSnapshotAllowedTime;

protected:
	/** Find the authoritative (server) world manager without touching its internals. */
	AVoxelWorldManager* GetAuthoritativeWorldManager() const;
};
