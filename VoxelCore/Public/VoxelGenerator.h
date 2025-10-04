#pragma once

#include "CoreMinimal.h"
#include "ChunkHelpers.h"
#include "ChunkConfig.h"
#include "VoxelChunk.h"
#include "FastNoiseLite.h"

/**
 * Deterministic chunk generator using FastNoiseLite.
 * - Produces a base terrain: stone deep, dirt a few layers, grass on top, air above.
 * - Deterministic based on seed + chunk coords.
 */
class FVoxelGenerator
{
public:
    FVoxelGenerator(int32 InSeed = DEFAULT_WORLD_SEED);

    /** Generate base chunk contents into OutChunk. Does not apply deltas. */
    void GenerateBaseChunk(const FChunkKey& Key, FVoxelChunkData& OutChunk);

    /** Generator parameters (tweakable) */
    void SetHeightScale(float InScale) { HeightScale = InScale; }
    void SetHeightOffset(float InOffset) { HeightOffset = InOffset; }
    void SetNoiseFrequency(float InFreq) { NoiseFrequency = InFreq; }

    int32 SampleColumnTopY(int32 WorldX, int32 WorldZ) const;

private:
    int32 Seed;
    FastNoiseLite NoiseHeight;
    float HeightScale;    // multiplier to convert noise to height
    float HeightOffset;   // additive offset
    float NoiseFrequency; // frequency scale for noise inputs

    FORCEINLINE int32 WorldHeightFromNoise(float NoiseValue) const
    {
        // noise expected in [-1,1]. Map to [0, CHUNK_SIZE_Y-1]
        float Norm = (NoiseValue + 1.0f) * 0.5f; // 0..1
        float TotalMaxHeight = static_cast<float>(CHUNK_SIZE_Y - 1);
        float H = Norm * HeightScale + HeightOffset;
        // Clamp to chunk height bounds
        int32 HeightInt = FMath::Clamp(static_cast<int32>(FMath::RoundToInt(H)), 1, CHUNK_SIZE_Y - 1);
        return HeightInt;
    }
};
