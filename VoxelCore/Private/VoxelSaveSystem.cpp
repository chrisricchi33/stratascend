// VoxelSaveSystem.cpp (final – matches FVoxelChunkData using TMap<int32,uint16> ModifiedBlocks)
#include "VoxelSaveSystem.h"
#include "VoxelChunk.h"
#include "ChunkConfig.h"

#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/MemoryReader.h"
#include "WorldPersistence.h"

static constexpr uint32 VCD_MAGIC = 0x44435631; // 'VCD1'
static constexpr uint16 VCD_VER = 1;

namespace VoxelSaveSystem
{

    // ---------------- paths ----------------
    static FString RootSeedDir(int32 Seed)
    {
        return FPaths::ProjectSavedDir() / TEXT("Voxel") / (TEXT("Seed_") + FString::FromInt(Seed)) / TEXT("Chunks");
    }

    static FString GetSeedChunkPath(int32 Seed, const FChunkKey& Key)
    {
        return RootSeedDir(Seed) / FString::Printf(TEXT("%d_%d.bin"), Key.X, Key.Z);
    }

    static FString GetWorldChunkPath(const FString& WorldName, const FChunkKey& Key)
    {
        return VoxelPaths::ChunksDir(WorldName) / FString::Printf(TEXT("%d_%d.bin"), Key.X, Key.Z);
    }

    // ---------------- binary format ----------------
    // [magic][ver][num:int32] { index:int32, id:uint8 } * num
    static bool WriteDeltaToBytes(const FVoxelChunkData& Data, TArray<uint8>& Out)
    {
        FBufferArchive Ar;
        uint32 Magic = VCD_MAGIC; Ar << Magic;
        uint16 Ver = VCD_VER;   Ar << Ver;

        int32 Num = Data.ModifiedBlocks.Num();
        Ar << Num;

        if (Num > 0)
        {
            for (const TPair<int32, uint16>& P : Data.ModifiedBlocks)
            {
                int32 Index = P.Key;
                uint8 Id = (uint8)P.Value;
                Ar << Index;
                Ar << Id;
            }
        }
        Out = MoveTemp(Ar);
        return true;
    }

    static bool ReadDeltaFromBytes(FVoxelChunkData& Data, const TArray<uint8>& In)
    {
        if (In.Num() <= 0) return false;

        FMemoryReader R(const_cast<TArray<uint8>&>(In));

        uint32 Magic = 0; R << Magic; if (Magic != VCD_MAGIC) return false;
        uint16 Ver = 0; R << Ver;   if (Ver != VCD_VER)   return false;

        int32 Num = 0; R << Num; if (Num < 0) return false;

        Data.ModifiedBlocks.Empty(Num);
        for (int32 i = 0; i < Num; ++i)
        {
            int32 Index = 0; uint8 Id = 0;
            R << Index; R << Id;
            Data.ModifiedBlocks.Add(Index, (uint16)Id);
        }
        return true;
    }

    static bool LoadDeltaFromPath(const FString& Path, FVoxelChunkData& Data)
    {
        TArray<uint8> Bytes;
        if (!FFileHelper::LoadFileToArray(Bytes, *Path)) return false;
        return ReadDeltaFromBytes(Data, Bytes);
    }

    static bool SaveDeltaToPath(const FString& Path, const FVoxelChunkData& Data)
    {
        IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
        PF.CreateDirectoryTree(*FPaths::GetPath(Path));

        TArray<uint8> Bytes;
        WriteDeltaToBytes(Data, Bytes);
        return FFileHelper::SaveArrayToFile(Bytes, *Path);
    }

    // ---------------- public API ----------------

    // Legacy seed-based IO (kept for back-compat if you still call it)
    bool LoadDelta(int32 Seed, FVoxelChunkData& Data)
    {
        return LoadDeltaFromPath(GetSeedChunkPath(Seed, Data.Key), Data);
    }

    bool SaveDelta(int32 Seed, const FVoxelChunkData& Data)
    {
        return SaveDeltaToPath(GetSeedChunkPath(Seed, Data.Key), Data);
    }

    // World-name based IO (new canonical path)
    bool LoadDeltaByWorld(const FString& WorldName, FVoxelChunkData& Data)
    {
        return LoadDeltaFromPath(GetWorldChunkPath(WorldName, Data.Key), Data);
    }

    bool SaveDeltaByWorld(const FString& WorldName, const FVoxelChunkData& Data)
    {
        return SaveDeltaToPath(GetWorldChunkPath(WorldName, Data.Key), Data);
    }

} // namespace VoxelSaveSystem
