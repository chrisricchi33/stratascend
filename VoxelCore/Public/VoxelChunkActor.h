#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "VoxelChunk.h" // <-- for FVoxelChunkData
#include "VoxelChunkActor.generated.h"

UCLASS()
class AVoxelChunkActor : public AActor
{
    GENERATED_BODY()
public:
    AVoxelChunkActor();

    UPROPERTY(VisibleAnywhere)
    UProceduralMeshComponent* ProcMesh;

    UPROPERTY(EditAnywhere, Category = "Voxel")
    float BlockSize = 100.f;

    // NEW: show/hide mesh without affecting collision
    UFUNCTION(BlueprintCallable, Category = "Voxel|Chunk")
    void SetRenderMeshes(bool bInRender);

    UFUNCTION(BlueprintCallable, Category = "Voxel|Chunk")
    bool GetRenderMeshes() const { return bRenderMeshes; }

    UPROPERTY(EditAnywhere, Category = "Voxel")
    bool bRenderMeshes = true;

    // Existing:
    void BuildFromBuffers(const TArray<FVector>& Vertices,
        const TArray<int32>& Triangles,
        const TArray<FVector>& Normals,
        const TArray<FVector2D>& UVs,
        const TArray<FLinearColor>& Colors,
        const TArray<FProcMeshTangent>& Tangents,
        UMaterialInterface* UseMaterial);

    // NEW: used by VoxelChunkSpawnCommand.cpp
    void BuildFromChunk(const FVoxelChunkData& Chunk, float InBlockSize, UMaterialInterface* UseMaterial);
};
