#pragma once

#include "CoreMinimal.h"
#include <cstdint>
#include "VoxelTypes.generated.h"

// -----------------------------------------------------------------------------
// Core block ids (code-only).
// -----------------------------------------------------------------------------
enum class EBlockId : uint8
{
	Air = 0,
	Dirt = 1,
	Grass = 2,
	Stone = 3,
	Sand = 4,

	// New blocks (do not reorder the first five)
	Wood = 5,   // planks (solid)
	Water = 6,   // liquid (we’ll handle translucent material later)
	Gravel = 7,   // loose stone (solid)
	Snow = 8,   // snow cover (solid)
	Log = 9,   // tree trunk (solid)
	Leaves = 10,  // foliage (non-opaque visual, but leave collision as your current system)
};

// -----------------------------------------------------------------------------
// Block registry entry (code-only).
// -----------------------------------------------------------------------------
struct FBlockInfo
{
	uint8 Id = static_cast<uint8>(EBlockId::Air);
	bool  bIsSolid = false;
	bool  bIsTransparent = true;
	uint8 MaterialIndex = 0;

	FBlockInfo() = default;
	FBlockInfo(uint8 InId, bool bSolid, bool bTransparent, uint8 InMatIdx)
		: Id(InId), bIsSolid(bSolid), bIsTransparent(bTransparent), MaterialIndex(InMatIdx) {
	}
};

// ============================================================================
// PHASE 6 — Blueprint-safe edit payloads
// ============================================================================

UENUM(BlueprintType)
enum class EVoxelEditAction : uint8
{
	Place  UMETA(DisplayName = "Place"),
	Remove UMETA(DisplayName = "Remove"),
};

/** Client → Server request (from BP line trace). */
USTRUCT(BlueprintType)
struct FBlockEditRequest
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite) FVector WorldHitLocation = FVector::ZeroVector;
	UPROPERTY(BlueprintReadWrite) FVector HitNormal = FVector::ZeroVector;

	/** For Place: integer value of EBlockId (0 == Air). For Remove: ignored. */
	UPROPERTY(BlueprintReadWrite) int32 NewBlockId = 0;

	UPROPERTY(BlueprintReadWrite) EVoxelEditAction Action = EVoxelEditAction::Place;

	/** Client-claimed reach (server revalidates/clamps). */
	UPROPERTY(BlueprintReadWrite) float ClaimedReach = 400.f;
};

/** Server → Clients compact op: identifies a single edited cell and result. */
USTRUCT(BlueprintType)
struct FBlockEditOp
{
	GENERATED_BODY()

	/** Chunk coordinates exposed to BP. */
	UPROPERTY(BlueprintReadWrite) FIntPoint ChunkXZ = FIntPoint(0, 0);

	/** Flat local index in the chunk (0..CHUNK_VOLUME-1). */
	UPROPERTY(BlueprintReadWrite) int32 LocalIndex = 0;

	/** Resulting block id (int value of EBlockId). */
	UPROPERTY(BlueprintReadWrite) int32 NewBlockId = 0;
};
