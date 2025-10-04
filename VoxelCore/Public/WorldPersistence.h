#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameFramework/Character.h"
#include "VoxelCore.h" // for VOXELCORE_API
#include "WorldPersistence.generated.h"

// ------------ Paths ------------
namespace VoxelPaths
{
	VOXELCORE_API FString WorldsRoot();                           // Saved/Voxel/Worlds/
	VOXELCORE_API FString WorldDir(const FString& WorldName);     // .../Worlds/<WorldName>/
	VOXELCORE_API FString ManifestPath(const FString& WorldName); // .../world.json
	VOXELCORE_API FString PlayersDir(const FString& WorldName);   // .../Players/
	VOXELCORE_API FString PlayerFile(const FString& WorldName, const FString& PlayerId); // .../Players/<id>.sav
	VOXELCORE_API FString ChunksDir(const FString& WorldName);    // .../Chunks/
}

// ------------ Data ------------
USTRUCT(BlueprintType)
struct FWorldMeta
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString   WorldName;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32     Seed = 1337;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FDateTime Created;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FDateTime LastPlayed;
};

USTRUCT(BlueprintType)
struct FPlayerBlob
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString    PlayerId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FTransform Transform;
	// Raw bytes of SaveGame-only vars of the Character
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<uint8> SaveBytes;
};

// ------------ Library ------------
UCLASS()
class VOXELCORE_API UWorldPersistenceLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	// Worlds
	UFUNCTION(BlueprintCallable, Category = "Voxel|Worlds")
	static bool WorldExists(const FString& WorldName);

	UFUNCTION(BlueprintCallable, Category = "Voxel|Worlds")
	static bool CreateOrTouchWorld(const FString& WorldName, int32 Seed, FWorldMeta& OutMeta, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "Voxel|Worlds")
	static void ListWorlds(TArray<FWorldMeta>& OutWorlds);

	UFUNCTION(BlueprintCallable, Category = "Voxel|Worlds")
	static bool LoadWorldMeta(const FString& WorldName, FWorldMeta& OutMeta);

	// Player blobs
	UFUNCTION(BlueprintCallable, Category = "Voxel|Players")
	static bool SavePlayerFromCharacter(const FString& WorldName, const FString& PlayerId, ACharacter* Character, FString& OutError);

	UFUNCTION(BlueprintCallable, Category = "Voxel|Players")
	static bool LoadPlayerToCharacter(const FString& WorldName, const FString& PlayerId, ACharacter* Character, FTransform& OutTransform);

	UFUNCTION(BlueprintCallable, Category = "Voxel|Players")
	static bool GetLastPlayerTransform(const FString& WorldName, const FString& PlayerId, FTransform& OutTransform);

	// Spawning helpers
	UFUNCTION(BlueprintCallable, Category = "Voxel|Spawning")
	static bool GetSurfaceSpawnAtOrigin(int32 Seed, float BlockSize, FTransform& OutSpawnTransform);

	// Streaming readiness
	UFUNCTION(BlueprintCallable, Category = "Voxel|Streaming")
	static bool AreChunksReadyAroundLocation(AActor* WorldManager, const FVector& WorldLocation);

	UFUNCTION(BlueprintCallable, Category = "Voxel|Streaming")
	static bool AreChunksReadyAroundLocationClamped(AActor* WorldManager, const FVector& WorldLocation, int32 ClampRadius);

	UFUNCTION(BlueprintCallable, Category = "Voxel|Streaming")
	static void ChunkReadinessDetails(AActor* WorldManager, const FVector& WorldLocation, int32 ClampRadius, int32& OutNeeded, int32& OutReady);


private:
	static bool SerializeSaveGameOnly(UObject* Obj, TArray<uint8>& OutBytes);
	static bool DeserializeSaveGameOnly(UObject* Obj, const TArray<uint8>& InBytes);
};
