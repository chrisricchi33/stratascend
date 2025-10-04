#include "VoxelChunkActor.h"
#include "Kismet/KismetMathLibrary.h"
#include "KismetProceduralMeshLibrary.h"
#include "VoxelMesher.h" // <-- for FVoxelMesher_Naive

AVoxelChunkActor::AVoxelChunkActor()
{
    PrimaryActorTick.bCanEverTick = false;

    ProcMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ProcMesh"));
    SetRootComponent(ProcMesh);

    ProcMesh->bUseAsyncCooking = true;
    ProcMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    ProcMesh->SetCollisionObjectType(ECC_WorldStatic);
    ProcMesh->SetCollisionResponseToAllChannels(ECR_Block);
}

void AVoxelChunkActor::BuildFromBuffers(const TArray<FVector>& Vertices,
    const TArray<int32>& Triangles,
    const TArray<FVector>& Normals,
    const TArray<FVector2D>& UVs,
    const TArray<FLinearColor>& Colors,
    const TArray<FProcMeshTangent>& Tangents,
    UMaterialInterface* UseMaterial)
{
    // Copy inputs so we can fix-up if needed
    TArray<FVector> UseNormals = Normals;
    TArray<FProcMeshTangent> UseTangents = Tangents;

    auto NeedsRecalc = [&]()
        {
            if (Vertices.Num() == 0 || Triangles.Num() == 0) return false; // nothing to draw anyway
            if (UseNormals.Num() != Vertices.Num()) return true;
            // if any normal is near-zero length, treat as invalid
            for (int32 i = 0; i < UseNormals.Num(); ++i)
            {
                if (UseNormals[i].SizeSquared() < 0.25f) return true; // length < 0.5
            }
            // tangents optional; if present, basic sanity
            if (UseTangents.Num() > 0 && UseTangents.Num() != Vertices.Num()) return true;
            return false;
        };

    if (NeedsRecalc())
    {
        TArray<FVector> RecalcNormals;
        TArray<FProcMeshTangent> RecalcTangents;
        // Will compute flat-ish normals from triangles + UV seams
        UKismetProceduralMeshLibrary::CalculateTangentsForMesh(
            Vertices, Triangles, UVs, RecalcNormals, RecalcTangents);

        UseNormals = MoveTemp(RecalcNormals);
        UseTangents = MoveTemp(RecalcTangents);
    }

    ProcMesh->ClearAllMeshSections();
    ProcMesh->CreateMeshSection_LinearColor(
        0, Vertices, Triangles, UseNormals, UVs, Colors, UseTangents, /*bCreateCollision*/ true);

    // Collision (as you had it)
    ProcMesh->bUseAsyncCooking = true;
    ProcMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    ProcMesh->SetCollisionObjectType(ECC_WorldStatic);
    ProcMesh->SetCollisionResponseToAllChannels(ECR_Block);

    ProcMesh->SetVisibleInRayTracing(false);

    // ✅ Ensure it participates in lighting/shadows
    ProcMesh->SetCastShadow(true);
    ProcMesh->bCastDynamicShadow = true;
    ProcMesh->bAffectDynamicIndirectLighting = true;
    ProcMesh->bAffectDistanceFieldLighting = true;

    if (UseMaterial)
    {
        ProcMesh->SetMaterial(0, UseMaterial);
    }

    ProcMesh->SetVisibility(bRenderMeshes, /*bPropagateToChildren=*/true);
}

void AVoxelChunkActor::BuildFromChunk(const FVoxelChunkData& Chunk, float InBlockSize, UMaterialInterface* UseMaterial)
{
    BlockSize = InBlockSize;

    TArray<FVector> V;
    TArray<int32>   I;
    TArray<FVector> N;
    TArray<FVector2D> UV;
    TArray<FLinearColor> C;
    TArray<FProcMeshTangent> T;

    // Naive mesher (Phase 3 path)
    FVoxelMesher_Naive::BuildMesh(Chunk, BlockSize, V, I, N, UV, C, T);

    BuildFromBuffers(V, I, N, UV, C, T, UseMaterial);
}

void AVoxelChunkActor::SetRenderMeshes(bool bInRender)
{
    bRenderMeshes = bInRender;
    if (ProcMesh)
    {
        ProcMesh->SetVisibility(bRenderMeshes, true);
    }
}


