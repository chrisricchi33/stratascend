#include "WorldPersistence.h"
#include "VoxelWorldManager.h"
#include "VoxelGenerator.h"
#include "VoxelChunk.h"
#include "ChunkConfig.h"

#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/BufferArchive.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "JsonObjectConverter.h"

namespace VoxelPaths
{
	static FString SavedRoot()
	{
		return FPaths::ProjectSavedDir() / TEXT("Voxel");
	}

	FString WorldsRoot()
	{
		return SavedRoot() / TEXT("Worlds");
	}

	FString WorldDir(const FString& WorldName)
	{
		return WorldsRoot() / WorldName;
	}

	FString ManifestPath(const FString& WorldName)
	{
		return WorldDir(WorldName) / TEXT("world.json");
	}

	FString PlayersDir(const FString& WorldName)
	{
		return WorldDir(WorldName) / TEXT("Players");
	}

	static FString SanitizeId(const FString& In)
	{
		FString Out = In;
		for (TCHAR& Ch : Out)
		{
			const bool bOk =
				(Ch >= '0' && Ch <= '9') ||
				(Ch >= 'A' && Ch <= 'Z') ||
				(Ch >= 'a' && Ch <= 'z') ||
				(Ch == '_' || Ch == '-');
			if (!bOk) Ch = '_';
		}
		// Empty fallback
		if (Out.IsEmpty()) Out = TEXT("Player");
		return Out;
	}

	FString PlayerFile(const FString& WorldName, const FString& PlayerId)
	{
		const FString Safe = SanitizeId(PlayerId);
		return PlayersDir(WorldName) / (Safe + TEXT(".sav"));
	}


	FString ChunksDir(const FString& WorldName)
	{
		return WorldDir(WorldName) / TEXT("Chunks");
	}
}

// --------- Serialization helpers ---------
bool UWorldPersistenceLibrary::SerializeSaveGameOnly(UObject* Obj, TArray<uint8>& OutBytes)
{
	if (!Obj) return false;
	OutBytes.Reset();
	FMemoryWriter MemWriter(OutBytes, true);
	FObjectAndNameAsStringProxyArchive Ar(MemWriter, false);
	Ar.ArIsSaveGame = true; // only properties marked SaveGame
	Obj->Serialize(Ar);
	return true;
}

bool UWorldPersistenceLibrary::DeserializeSaveGameOnly(UObject* Obj, const TArray<uint8>& InBytes)
{
	if (!Obj) return false;
	FMemoryReader MemReader(InBytes, true);
	FObjectAndNameAsStringProxyArchive Ar(MemReader, true);
	Ar.ArIsSaveGame = true;
	Obj->Serialize(Ar);
	return true;
}

// --------- Worlds ---------
bool UWorldPersistenceLibrary::WorldExists(const FString& WorldName)
{
	return IFileManager::Get().DirectoryExists(*VoxelPaths::WorldDir(WorldName));
}

bool UWorldPersistenceLibrary::CreateOrTouchWorld(const FString& WorldName, int32 Seed, FWorldMeta& OutMeta, FString& OutError)
{
	OutError.Reset();
	if (WorldName.IsEmpty())
	{
		OutError = TEXT("World name cannot be empty");
		return false;
	}

	// Ensure dirs (use platform file)
	IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
	PF.CreateDirectoryTree(*VoxelPaths::WorldDir(WorldName));
	PF.CreateDirectoryTree(*VoxelPaths::PlayersDir(WorldName));
	PF.CreateDirectoryTree(*VoxelPaths::ChunksDir(WorldName));

	// Read or create manifest
	FString ManifestPath = VoxelPaths::ManifestPath(WorldName);
	FWorldMeta Meta;
	if (FPaths::FileExists(ManifestPath))
	{
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *ManifestPath))
		{
			OutError = TEXT("Failed to read existing world manifest");
			return false;
		}
		if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Meta, 0, 0))
		{
			OutError = TEXT("Failed to parse world manifest");
			return false;
		}
		Meta.LastPlayed = FDateTime::UtcNow();
	}
	else
	{
		Meta.WorldName = WorldName;
		Meta.Seed = Seed;
		Meta.Created = FDateTime::UtcNow();
		Meta.LastPlayed = Meta.Created;
	}

	// Write manifest
	FString OutJson;
	if (!FJsonObjectConverter::UStructToJsonObjectString(Meta, OutJson))
	{
		OutError = TEXT("Failed to serialize world manifest");
		return false;
	}
	if (!FFileHelper::SaveStringToFile(OutJson, *ManifestPath))
	{
		OutError = TEXT("Failed to write world manifest");
		return false;
	}

	OutMeta = Meta;
	return true;
}

void UWorldPersistenceLibrary::ListWorlds(TArray<FWorldMeta>& OutWorlds)
{
	OutWorlds.Reset();
	TArray<FString> WorldDirs;
	IFileManager::Get().FindFiles(WorldDirs, *(VoxelPaths::WorldsRoot() / TEXT("*")), false, true);
	for (const FString& Name : WorldDirs)
	{
		FWorldMeta Meta;
		if (LoadWorldMeta(Name, Meta))
		{
			OutWorlds.Add(Meta);
		}
	}
	OutWorlds.Sort([](const FWorldMeta& A, const FWorldMeta& B)
		{
			return A.LastPlayed > B.LastPlayed;
		});
}

bool UWorldPersistenceLibrary::LoadWorldMeta(const FString& WorldName, FWorldMeta& OutMeta)
{
	const FString ManifestPath = VoxelPaths::ManifestPath(WorldName);
	if (!FPaths::FileExists(ManifestPath)) return false;
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *ManifestPath)) return false;
	return FJsonObjectConverter::JsonObjectStringToUStruct(Json, &OutMeta, 0, 0);
}

// --------- Players ---------
static bool ReadBlob(const FString& WorldName, const FString& PlayerId, FPlayerBlob& Out)
{
	const FString FilePath = VoxelPaths::PlayerFile(WorldName, PlayerId);
	TArray<uint8> Data;
	if (!FFileHelper::LoadFileToArray(Data, *FilePath)) return false;

	FMemoryReader Reader(Data);
	Reader << Out.PlayerId;          // stored ID (may not exactly match later lookup)
	Reader << Out.Transform;
	Reader << Out.SaveBytes;

	// Do NOT require exact string equality; file presence is authoritative.
	return true;
}


bool UWorldPersistenceLibrary::SavePlayerFromCharacter(const FString& WorldName, const FString& PlayerId, ACharacter* Character, FString& OutError)
{
	OutError.Reset();
	if (!Character) { OutError = TEXT("Character is null"); return false; }
	if (!WorldExists(WorldName)) { OutError = TEXT("World does not exist"); return false; }

	FPlayerBlob Blob;
	Blob.PlayerId = PlayerId;
	Blob.Transform = Character->GetActorTransform();

	if (!SerializeSaveGameOnly(Character, Blob.SaveBytes))
	{
		OutError = TEXT("Failed to serialize character SaveGame properties");
		return false;
	}

	// Write blob file
	const FString FilePath = VoxelPaths::PlayerFile(WorldName, PlayerId);
	FBufferArchive Ar;
	Ar << Blob.PlayerId;
	Ar << Blob.Transform;
	Ar << Blob.SaveBytes;
	const bool bOK = FFileHelper::SaveArrayToFile(Ar, *FilePath);
	Ar.FlushCache();
	Ar.Empty();
	if (!bOK)
	{
		OutError = TEXT("Failed to write player blob file");
		return false;
	}
	return true;
}

bool UWorldPersistenceLibrary::LoadPlayerToCharacter(const FString& WorldName, const FString& PlayerId, ACharacter* Character, FTransform& OutTransform)
{
	if (!Character) return false;

	FPlayerBlob Blob;
	if (!ReadBlob(WorldName, PlayerId, Blob)) return false;

	OutTransform = Blob.Transform;
	return DeserializeSaveGameOnly(Character, Blob.SaveBytes);
}

bool UWorldPersistenceLibrary::GetLastPlayerTransform(const FString& WorldName, const FString& PlayerId, FTransform& OutTransform)
{
	FPlayerBlob Blob;
	if (!ReadBlob(WorldName, PlayerId, Blob)) return false;
	OutTransform = Blob.Transform;
	return true;
}

// --------- Spawning / surface at (0,0) ---------
bool UWorldPersistenceLibrary::GetSurfaceSpawnAtOrigin(int32 Seed, float BlockSize, FTransform& OutSpawnTransform)
{
	const FChunkKey Key(0, 0);
	FVoxelChunkData Data(Key);
	FVoxelGenerator Gen(Seed);
	Gen.GenerateBaseChunk(Key, Data);

	const int32 X = CHUNK_SIZE_X / 2;
	const int32 Z = CHUNK_SIZE_Z / 2;

	int32 SurfaceY = CHUNK_SIZE_Y - 1;
	for (int32 y = CHUNK_SIZE_Y - 1; y >= 0; --y)
	{
		if (Data.GetBlockAt(X, y, Z) != EBlockId::Air)
		{
			SurfaceY = y; break;
		}
	}

	const FVector WorldLocation(
		(double)Key.X * CHUNK_SIZE_X * BlockSize + X * BlockSize + 0.5 * BlockSize,
		(double)Key.Z * CHUNK_SIZE_Z * BlockSize + Z * BlockSize + 0.5 * BlockSize,
		(SurfaceY + 2) * BlockSize);
	OutSpawnTransform = FTransform(FRotator::ZeroRotator, WorldLocation, FVector::OneVector);
	return true;
}

// --------- Readiness around a location ---------
// (Requires: friend class UWorldPersistenceLibrary; in AVoxelWorldManager)
bool UWorldPersistenceLibrary::AreChunksReadyAroundLocation(AActor* WorldManager, const FVector& WorldLocation)
{
	const AVoxelWorldManager* Mgr = Cast<AVoxelWorldManager>(WorldManager);
	if (!Mgr) return false;

	const int32 R = Mgr->RenderRadiusChunks;
	const double CSX = (double)CHUNK_SIZE_X * Mgr->BlockSize;
	const double CSZ = (double)CHUNK_SIZE_Z * Mgr->BlockSize;

	const int32 Cx = FMath::FloorToInt(WorldLocation.X / CSX);
	const int32 Cz = FMath::FloorToInt(WorldLocation.Y / CSZ);

	for (int32 dz = -R; dz <= R; ++dz)
		for (int32 dx = -R; dx <= R; ++dx)
		{
			const FChunkKey K(Cx + dx, Cz + dz);
			const auto* Rec = Mgr->Loaded.Find(K);
			if (!Rec || !Rec->Actor.IsValid())
			{
				return false;
			}
		}
	return true;
}

bool UWorldPersistenceLibrary::AreChunksReadyAroundLocationClamped(AActor* WM, const FVector& WorldLocation, int32 ClampRadius)
{
	const AVoxelWorldManager* Mgr = Cast<AVoxelWorldManager>(WM);
	if (!Mgr) return false;

	const int32 R = FMath::Clamp(ClampRadius, 1, Mgr->RenderRadiusChunks);
	const double CSX = (double)CHUNK_SIZE_X * Mgr->BlockSize;
	const double CSZ = (double)CHUNK_SIZE_Z * Mgr->BlockSize;

	const int32 Cx = FMath::FloorToInt(WorldLocation.X / CSX);
	const int32 Cz = FMath::FloorToInt(WorldLocation.Y / CSZ);

	for (int32 dz = -R; dz <= R; ++dz)
		for (int32 dx = -R; dx <= R; ++dx)
		{
			const FChunkKey K(Cx + dx, Cz + dz);
			const auto* Rec = Mgr->Loaded.Find(K);
			if (!Rec || !Rec->Actor.IsValid())
				return false;
		}
	return true;
}

void UWorldPersistenceLibrary::ChunkReadinessDetails(AActor* WM, const FVector& WorldLocation, int32 ClampRadius, int32& OutNeeded, int32& OutReady)
{
	OutNeeded = 0; OutReady = 0;
	const AVoxelWorldManager* Mgr = Cast<AVoxelWorldManager>(WM);
	if (!Mgr) return;

	const int32 R = FMath::Clamp(ClampRadius, 1, Mgr->RenderRadiusChunks);
	const double CSX = (double)CHUNK_SIZE_X * Mgr->BlockSize;
	const double CSZ = (double)CHUNK_SIZE_Z * Mgr->BlockSize;

	const int32 Cx = FMath::FloorToInt(WorldLocation.X / CSX);
	const int32 Cz = FMath::FloorToInt(WorldLocation.Y / CSZ);

	for (int32 dz = -R; dz <= R; ++dz)
		for (int32 dx = -R; dx <= R; ++dx)
		{
			++OutNeeded;
			const FChunkKey K(Cx + dx, Cz + dz);
			const auto* Rec = Mgr->Loaded.Find(K);
			if (Rec && Rec->Actor.IsValid())
				++OutReady;
		}
}

