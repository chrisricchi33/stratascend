#pragma once

#include "CoreMinimal.h"
#include "VoxelChunk.h"
#include "VoxelTypes.h"
#include "ProceduralMeshComponent.h"

/**
 * Naive mesher that emits visible faces only.
 * - Produces scaled vertex positions (in world units) given BlockSize.
 * - Produces simple UVs suitable for a tiled atlas.
 */
class FVoxelMesher_Naive
{
public:
    /** Build mesh arrays from the chunk.
     * BlockSize = size of one cube along each axis in Unreal units (e.g. 100)
     */
    static void BuildMesh(const FVoxelChunkData& Chunk, float BlockSize,
        TArray<FVector>& OutVertices,
        TArray<int32>& OutTriangles,
        TArray<FVector>& OutNormals,
        TArray<FVector2D>& OutUVs,
        TArray<FLinearColor>& OutColors,
        TArray<FProcMeshTangent>& OutTangents);

private:
    // helper: returns true if neighbor at world-local (x+nx,y+ny,z+nz) is empty (air)
    static bool IsAirNeighbor(const FVoxelChunkData& Chunk, int32 X, int32 Y, int32 Z, int32 NX, int32 NY, int32 NZ);

    // Simple atlas mapping: returns bottom-left UV and tile size (uTile,vTile)
    static void GetAtlasUVForBlock(EBlockId Id, FVector2D& OutUV0, FVector2D& OutTileSize);
};
