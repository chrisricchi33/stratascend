#include "VoxelMesher.h"
#include "ChunkConfig.h"
#include "ChunkHelpers.h"
#include "Math/UnrealMathUtility.h"
#include "ProceduralMeshComponent.h" // for FProcMeshTangent

void FVoxelMesher_Naive::BuildMesh(const FVoxelChunkData& Chunk, float BlockSize,
    TArray<FVector>& OutVertices,
    TArray<int32>& OutTriangles,
    TArray<FVector>& OutNormals,
    TArray<FVector2D>& OutUVs,
    TArray<FLinearColor>& OutColors,
    TArray<FProcMeshTangent>& OutTangents)
{
    OutVertices.Reset();
    OutTriangles.Reset();
    OutNormals.Reset();
    OutUVs.Reset();
    OutColors.Reset();
    OutTangents.Reset();

    const float Half = BlockSize * 0.5f;

    for (int32 X = 0; X < CHUNK_SIZE_X; ++X)
    {
        for (int32 Z = 0; Z < CHUNK_SIZE_Z; ++Z)
        {
            for (int32 Y = 0; Y < CHUNK_SIZE_Y; ++Y)
            {
                EBlockId Id = Chunk.GetBlockAt(X, Y, Z);
                if (Id == EBlockId::Air) continue;

                const float WorldX = static_cast<float>(X) * BlockSize;
                const float WorldY = static_cast<float>(Z) * BlockSize;
                const float WorldZ = static_cast<float>(Y) * BlockSize;

                const FVector Min(WorldX - Half, WorldY - Half, WorldZ - Half);
                const FVector Max(WorldX + Half, WorldY + Half, WorldZ + Half);

                const FVector
                    B00(Min.X, Min.Y, Min.Z), B10(Max.X, Min.Y, Min.Z),
                    B11(Max.X, Max.Y, Min.Z), B01(Min.X, Max.Y, Min.Z),
                    T00(Min.X, Min.Y, Max.Z), T10(Max.X, Min.Y, Max.Z),
                    T11(Max.X, Max.Y, Max.Z), T01(Min.X, Max.Y, Max.Z);

                FVector2D UV0, Tile;
                GetAtlasUVForBlock(Id, UV0, Tile);

                FLinearColor Color = FLinearColor::White;
                switch (Id)
                {
                case EBlockId::Grass: Color = FLinearColor(0.1f, 0.8f, 0.1f); break;
                case EBlockId::Dirt:  Color = FLinearColor(0.45f, 0.28f, 0.13f); break;
                case EBlockId::Stone: Color = FLinearColor(0.5f, 0.5f, 0.5f); break;
                default: break;
                }

                auto PushFace = [&](const FVector& A, const FVector& B, const FVector& C, const FVector& D, const FVector& Normal, bool bFlipTopUVs = false)
                    {
                        const int32 Base = OutVertices.Num();

                        OutVertices.Add(A); // 0
                        OutVertices.Add(B); // 1
                        OutVertices.Add(C); // 2
                        OutVertices.Add(D); // 3

                        // Flipped winding order for outward normals
                        OutTriangles.Add(Base + 0);
                        OutTriangles.Add(Base + 2);
                        OutTriangles.Add(Base + 1);
                        OutTriangles.Add(Base + 0);
                        OutTriangles.Add(Base + 3);
                        OutTriangles.Add(Base + 2);

                        OutNormals.Add(Normal);
                        OutNormals.Add(Normal);
                        OutNormals.Add(Normal);
                        OutNormals.Add(Normal);

                        // UVs
                        if (!bFlipTopUVs)
                        {
                            const FVector2D U00(UV0.X, UV0.Y);
                            const FVector2D U10(UV0.X + Tile.X, UV0.Y);
                            const FVector2D U11(UV0.X + Tile.X, UV0.Y + Tile.Y);
                            const FVector2D U01(UV0.X, UV0.Y + Tile.Y);
                            OutUVs.Add(U00);
                            OutUVs.Add(U10);
                            OutUVs.Add(U11);
                            OutUVs.Add(U01);
                        }
                        else
                        {
                            // ? Corrected orientation for grass top
                            const FVector2D U00(UV0.X, UV0.Y + Tile.Y);
                            const FVector2D U10(UV0.X + Tile.X, UV0.Y + Tile.Y);
                            const FVector2D U11(UV0.X + Tile.X, UV0.Y);
                            const FVector2D U01(UV0.X, UV0.Y);
                            OutUVs.Add(U00);
                            OutUVs.Add(U10);
                            OutUVs.Add(U11);
                            OutUVs.Add(U01);
                        }

                        OutColors.Add(Color);
                        OutColors.Add(Color);
                        OutColors.Add(Color);
                        OutColors.Add(Color);

                        const FVector TangentDir = FVector::CrossProduct(FVector::UpVector, Normal).GetSafeNormal();
                        const FProcMeshTangent Tangent(TangentDir, false);
                        OutTangents.Add(Tangent);
                        OutTangents.Add(Tangent);
                        OutTangents.Add(Tangent);
                        OutTangents.Add(Tangent);
                    };

                auto IsAir = [&](int32 NX, int32 NY, int32 NZ)
                    {
                        if (NX < 0 || NX >= CHUNK_SIZE_X ||
                            NY < 0 || NY >= CHUNK_SIZE_Y ||
                            NZ < 0 || NZ >= CHUNK_SIZE_Z) return true;
                        return Chunk.GetBlockAt(NX, NY, NZ) == EBlockId::Air;
                    };

                if (IsAir(X + 1, Y, Z)) PushFace(B10, B11, T11, T10, FVector(1, 0, 0));    // +X
                if (IsAir(X - 1, Y, Z)) PushFace(B01, B00, T00, T01, FVector(-1, 0, 0));   // -X
                if (IsAir(X, Y, Z + 1)) PushFace(B11, B01, T01, T11, FVector(0, 1, 0));    // +Y (north)
                if (IsAir(X, Y, Z - 1)) PushFace(B00, B10, T10, T00, FVector(0, -1, 0));   // -Y (south)
                if (IsAir(X, Y + 1, Z)) PushFace(T00, T10, T11, T01, FVector(0, 0, 1), true); // +Z (top, flip UVs)
                if (IsAir(X, Y - 1, Z)) PushFace(B01, B11, B10, B00, FVector(0, 0, -1));   // -Z (bottom)
            }
        }
    }
}

bool FVoxelMesher_Naive::IsAirNeighbor(const FVoxelChunkData& Chunk, int32 X, int32 Y, int32 Z, int32 NX, int32 NY, int32 NZ)
{
    int32 NXAbs = X + NX;
    int32 NYAbs = Y + NY;
    int32 NZAbs = Z + NZ;

    if (NXAbs < 0 || NXAbs >= CHUNK_SIZE_X ||
        NYAbs < 0 || NYAbs >= CHUNK_SIZE_Y ||
        NZAbs < 0 || NZAbs >= CHUNK_SIZE_Z)
    {
        return true;
    }

    EBlockId Neighbor = Chunk.GetBlockAt(NXAbs, NYAbs, NZAbs);
    return (Neighbor == EBlockId::Air);
}

//void FVoxelMesher_Naive::GetAtlasUVForBlock(EBlockId Id, FVector2D& OutUV0, FVector2D& OutTileSize)
//{
//    const int32 TilesAcross = 4;
//    const float TileSize = 1.0f / static_cast<float>(TilesAcross);
//
//    int32 Slot = 0;
//    switch (Id)
//    {
//    case EBlockId::Grass: Slot = 0; break;
//    case EBlockId::Dirt:  Slot = 1; break;
//    case EBlockId::Stone: Slot = 2; break;
//    default: Slot = 3; break;
//    }
//
//    OutUV0 = FVector2D(Slot * TileSize, 0.0f);
//    OutTileSize = FVector2D(TileSize, 1.0f);
//}

// Current atlas: 1 row x 4 columns: [0]=Grass, [1]=Dirt, [2]=Stone, [3]=White/Blank
void FVoxelMesher_Naive::GetAtlasUVForBlock(EBlockId Id, FVector2D& OutUV0, FVector2D& OutTileSize)
{
    const float TileW = 1.0f / 4.0f; // 4 columns
    const float TileH = 1.0f / 4.0f; // 4 rows

    // Small padding to reduce bleeding when mips/filters kick in
    const float PadU = TileW * 0.03f;
    const float PadV = TileH * 0.03f;

    int32 slotX = 3; // default to a free/blank tile
    int32 slotY = 3;

    switch (Id)
    {
        // Row 0
    case EBlockId::Grass:  slotX = 0; slotY = 0; break;
    case EBlockId::Dirt:   slotX = 1; slotY = 0; break;
    case EBlockId::Stone:  slotX = 2; slotY = 0; break;
    case EBlockId::Sand:   slotX = 3; slotY = 0; break;

        // Row 1
    case EBlockId::Wood:   slotX = 0; slotY = 1; break; // planks
    case EBlockId::Gravel: slotX = 1; slotY = 1; break;
    case EBlockId::Snow:   slotX = 2; slotY = 1; break;
    case EBlockId::Log:    slotX = 3; slotY = 1; break; // log side grain

        // Row 2
    case EBlockId::Leaves: slotX = 0; slotY = 2; break;
    case EBlockId::Water:  slotX = 1; slotY = 2; break;

    default:               slotX = 3; slotY = 3; break; // free tile
    }

    OutUV0 = FVector2D(slotX * TileW + PadU, slotY * TileH + PadV);
    OutTileSize = FVector2D(TileW - 2.0f * PadU, TileH - 2.0f * PadV);
}
