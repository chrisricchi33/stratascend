#include "VoxelCore.h"
#include "ChunkConfig.h"
#include "VoxelTypes.h"
#include "FastNoiseLite.h"

static FAutoConsoleCommand CmdVoxelTestSetup(
    TEXT("Voxel.TestSetup"),
    TEXT("Runs voxel core setup test to validate indexing + noise"),
    FConsoleCommandDelegate::CreateStatic([]()
        {
            int32 X = 5, Y = 10, Z = 2;
            int32 Index = X + Y * CHUNK_SIZE_X + Z * CHUNK_SIZE_X * CHUNK_SIZE_Y;

            FString IndexMsg = FString::Printf(TEXT("Index from (5,10,2) = %d"), Index);
            UE_LOG(LogTemp, Log, TEXT("%s"), *IndexMsg);
            if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow, IndexMsg);

            int32 OutX = Index % CHUNK_SIZE_X;
            int32 OutY = (Index / CHUNK_SIZE_X) % CHUNK_SIZE_Y;
            int32 OutZ = Index / (CHUNK_SIZE_X * CHUNK_SIZE_Y);

            FString CoordMsg = FString::Printf(TEXT("XYZ from index: (%d, %d, %d)"), OutX, OutY, OutZ);
            UE_LOG(LogTemp, Log, TEXT("%s"), *CoordMsg);
            if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan, CoordMsg);

            FastNoiseLite Noise;
            Noise.SetSeed(DEFAULT_WORLD_SEED);
            Noise.SetNoiseType(FastNoiseLite::NoiseType_OpenSimplex2);
            float Value = Noise.GetNoise(10.0f, 20.0f, 30.0f);

            FString NoiseMsg = FString::Printf(TEXT("Noise(10,20,30) = %f"), Value);
            UE_LOG(LogTemp, Log, TEXT("%s"), *NoiseMsg);
            if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green, NoiseMsg);
        })
);

