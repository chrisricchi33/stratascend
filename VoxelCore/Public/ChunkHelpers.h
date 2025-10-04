#pragma once
#include "CoreMinimal.h"
#include "ChunkConfig.h"

// Indexing convention:
// X in [0,CHUNK_SIZE_X), Y in [0,CHUNK_SIZE_Y) vertical, Z in [0,CHUNK_SIZE_Z)
// Index = X + Z * CHUNK_SIZE_X + Y * (CHUNK_SIZE_X * CHUNK_SIZE_Z)

inline int32 IndexFromXYZ(int32 X, int32 Y, int32 Z)
{
	return X + Z * CHUNK_SIZE_X + Y * (CHUNK_SIZE_X * CHUNK_SIZE_Z);
}


inline void XYZFromIndex(int32 Index, int32& OutX, int32& OutY, int32& OutZ)
{
	const int32 XYPlane = CHUNK_SIZE_X * CHUNK_SIZE_Z; // blocks per vertical layer
	OutY = Index / XYPlane;
	int32 Rem = Index - (OutY * XYPlane);
	OutZ = Rem / CHUNK_SIZE_X;
	OutX = Rem - (OutZ * CHUNK_SIZE_X);
}

// Simple chunk key (2D chunks: chunk X and chunk Z)
struct FChunkKey
{
	int32 X;
	int32 Z;


	FChunkKey() : X(0), Z(0) {}
	FChunkKey(int32 InX, int32 InZ) : X(InX), Z(InZ) {}


	bool operator==(const FChunkKey& Other) const
	{
		return X == Other.X && Z == Other.Z;
	}
};


// Hash function so we can use FChunkKey in TMap/TSet
FORCEINLINE uint32 GetTypeHash(const FChunkKey& Key)
{
	// Combine the two 32-bit ints into a hash
	return HashCombine(::GetTypeHash(Key.X), ::GetTypeHash(Key.Z));
}