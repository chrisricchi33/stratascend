#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VoxelTypes.h"
#include "VoxelEditLibrary.generated.h"

UCLASS()
class UVoxelEditLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Call this from your BP character on LMB/RMB to trace and request an edit. */
	UFUNCTION(BlueprintCallable, Category = "Voxel|Edits")
	static void Voxel_LineTraceAndRequestEdit(
		AActor* CharacterOrController,
		EVoxelEditAction Action,
		int32 NewBlockId,
		float MaxReach = 400.f
	);
};
