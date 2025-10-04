#pragma once

#include "CoreMinimal.h"
#include "ChunkConfig.h"     // CHUNK_SIZE_*
#include "ChunkHelpers.h"    // FChunkKey, IndexFromXYZ(...)
#include "VoxelTypes.h"      // EBlockId

// One chunk’s voxel data: base array + delta overrides (flat index -> id).
struct FVoxelChunkData
{
    // Identity
    FChunkKey Key; // chunk coords (X,Z)

    // Base blocks (generated). Size = CHUNK_VOLUME. Stores EBlockId as uint8.
    TArray<uint8> Blocks;

    // Deltas (persisted): localIndex -> blockId (16-bit for future-proofing).
    TMap<int32, uint16> ModifiedBlocks;

    // Ctors
    FVoxelChunkData() = default;

    explicit FVoxelChunkData(const FChunkKey& InKey)
        : Key(InKey)
    {
        Blocks.SetNumZeroed(CHUNK_VOLUME);      // default Air (0)
        ModifiedBlocks.Empty();
    }

    // Bounds check
    FORCEINLINE bool IsInBounds(int32 X, int32 Y, int32 Z) const
    {
        return X >= 0 && X < CHUNK_SIZE_X
            && Y >= 0 && Y < CHUNK_SIZE_Y
            && Z >= 0 && Z < CHUNK_SIZE_Z;
    }

    // Indexing (delegates to helpers: X + Z*X + Y*(X*Z))
    FORCEINLINE int32 IndexFromXYZLocal(int32 X, int32 Y, int32 Z) const
    {
        return IndexFromXYZ(X, Y, Z);
    }

    // Read with delta fallback
    FORCEINLINE EBlockId GetBlockAt(int32 X, int32 Y, int32 Z) const
    {
        if (!IsInBounds(X, Y, Z)) return EBlockId::Air;
        const int32 Index = IndexFromXYZLocal(X, Y, Z);

        if (const uint16* Ptr = ModifiedBlocks.Find(Index))
        {
            return static_cast<EBlockId>(*Ptr);
        }
        return static_cast<EBlockId>(Blocks.IsValidIndex(Index) ? Blocks[Index] : 0);
    }

    // Write: if equal to base value, drop delta; else store 16-bit delta.
    FORCEINLINE void SetBlockAt(int32 X, int32 Y, int32 Z, EBlockId NewId, bool bMarkModified = true)
    {
        if (!IsInBounds(X, Y, Z)) return;
        const int32 Index = IndexFromXYZLocal(X, Y, Z);

        if (!Blocks.IsValidIndex(Index)) return;

        const uint8 Raw = static_cast<uint8>(NewId);
        if (bMarkModified)
        {
            // If the new id equals the base Blocks value, remove the delta
            if (Blocks[Index] == Raw)
            {
                ModifiedBlocks.Remove(Index);
            }
            else
            {
                ModifiedBlocks.Add(Index, static_cast<uint16>(Raw));
            }
        }
        else
        {
            Blocks[Index] = Raw;
        }
    }

    FORCEINLINE void ClearDeltas()
    {
        ModifiedBlocks.Empty();
    }
};
