#pragma once
#include "CoreMinimal.h"
#include "VoxelChunk.h"
#include "VoxelCore.h" // VOXELCORE_API

namespace VoxelSaveSystem
{
	// Legacy (by seed) retained for back-compat if you still call it elsewhere
	VOXELCORE_API bool LoadDelta(int32 Seed, FVoxelChunkData& InOut);
	VOXELCORE_API bool SaveDelta(int32 Seed, const FVoxelChunkData& Data);

	// New (by world name)
	VOXELCORE_API bool LoadDeltaByWorld(const FString& WorldName, FVoxelChunkData& InOut);
	VOXELCORE_API bool SaveDeltaByWorld(const FString& WorldName, const FVoxelChunkData& Data);
}
