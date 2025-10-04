#include "VoxelGenerator.h"
#include "ChunkConfig.h"
#include "VoxelTypes.h"

FVoxelGenerator::FVoxelGenerator(int32 InSeed)
    : Seed(InSeed)
    , HeightScale(static_cast<float>(CHUNK_SIZE_Y) * 0.6f) // ~60% of vertical range
    , HeightOffset(static_cast<float>(CHUNK_SIZE_Y) * 0.2f) // base offset
    , NoiseFrequency(0.05f) // << increased frequency so noise varies more
{
    NoiseHeight.SetSeed(Seed);
    NoiseHeight.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
    NoiseHeight.SetFrequency(NoiseFrequency);
}

int32 FVoxelGenerator::SampleColumnTopY(int32 WorldX, int32 WorldZ) const
{
    // Use the same noise + mapping you use in GenerateBaseChunk
    const float NX = static_cast<float>(WorldX) * NoiseFrequency;
    const float NZ = static_cast<float>(WorldZ) * NoiseFrequency;
    const float NoiseVal = NoiseHeight.GetNoise(NX, NZ);
    return WorldHeightFromNoise(NoiseVal);
}


void FVoxelGenerator::GenerateBaseChunk(const FChunkKey& Key, FVoxelChunkData& OutChunk)
{
    OutChunk.Key = Key;

    if (OutChunk.Blocks.Num() != CHUNK_VOLUME)
    {
        OutChunk.Blocks.SetNumZeroed(CHUNK_VOLUME);
    }
    OutChunk.ClearDeltas();

    for (int32 LocalZ = 0; LocalZ < CHUNK_SIZE_Z; ++LocalZ)
    {
        for (int32 LocalX = 0; LocalX < CHUNK_SIZE_X; ++LocalX)
        {
            // World coordinates for noise input
            int32 WorldX = Key.X * CHUNK_SIZE_X + LocalX;
            int32 WorldZ = Key.Z * CHUNK_SIZE_Z + LocalZ;

            // Noise input (scaled floats)
            float NX = static_cast<float>(WorldX) * NoiseFrequency;
            float NZ = static_cast<float>(WorldZ) * NoiseFrequency;
            float NoiseVal = NoiseHeight.GetNoise(NX, NZ);

            // Map noise to usable chunk height
            int32 ColumnTopY = WorldHeightFromNoise(NoiseVal);

            for (int32 LocalY = 0; LocalY < CHUNK_SIZE_Y; ++LocalY)
            {
                int32 Index = IndexFromXYZ(LocalX, LocalY, LocalZ);

                if (LocalY > ColumnTopY)
                {
                    OutChunk.Blocks[Index] = static_cast<uint8>(EBlockId::Air);
                }
                else
                {
                    int32 Depth = ColumnTopY - LocalY;
                    if (Depth == 0)
                    {
                        OutChunk.Blocks[Index] = static_cast<uint8>(EBlockId::Grass);
                    }
                    else if (Depth <= 3)
                    {
                        OutChunk.Blocks[Index] = static_cast<uint8>(EBlockId::Dirt);
                    }
                    else
                    {
                        OutChunk.Blocks[Index] = static_cast<uint8>(EBlockId::Stone);
                    }
                }
            }
        }
    }
}
