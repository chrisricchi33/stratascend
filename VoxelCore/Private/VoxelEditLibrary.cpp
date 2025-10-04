#include "VoxelEditLibrary.h"                // MUST be first (IWYU)

#include "VoxelWorldManager.h"
#include "Kismet/GameplayStatics.h"
#include "VoxelPlayerController.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/EngineTypes.h"              // FHitResult, ECollisionChannel
#include "CollisionQueryParams.h"            // FCollisionQueryParams

void UVoxelEditLibrary::Voxel_LineTraceAndRequestEdit(
	AActor* CharacterOrController,
	EVoxelEditAction Action,
	int32 NewBlockId,
	float MaxReach
)
{
	if (!CharacterOrController) return;

	UWorld* World = CharacterOrController->GetWorld();
	if (!World) return;

	APlayerController* PC = nullptr;
	if (APawn* Pawn = Cast<APawn>(CharacterOrController))
	{
		PC = Cast<APlayerController>(Pawn->GetController());
	}
	else
	{
		PC = Cast<APlayerController>(CharacterOrController);
	}
	if (!PC) return;

	FVector ViewLoc; FRotator ViewRot;
	PC->GetPlayerViewPoint(ViewLoc, ViewRot);

	const FVector Dir = ViewRot.Vector();
	const float Reach = FMath::Clamp(MaxReach, 100.f, 800.f);
	const FVector TraceEnd = ViewLoc + Dir * Reach;

	FCollisionQueryParams QP(SCENE_QUERY_STAT(VoxelEditTrace), false);
	QP.AddIgnoredActor(CharacterOrController);

	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, ViewLoc, TraceEnd, ECollisionChannel::ECC_Visibility, QP))
		return;

	// Build the request and send to server
	FBlockEditRequest Req;
	Req.WorldHitLocation = Hit.ImpactPoint;
	Req.HitNormal = Hit.ImpactNormal;
	Req.Action = Action;
	Req.NewBlockId = NewBlockId;
	Req.ClaimedReach = Reach;

	if (AVoxelPlayerController* VPC = Cast<AVoxelPlayerController>(PC))
	{
		VPC->Server_RequestBlockEdit(Req);
	}
}

