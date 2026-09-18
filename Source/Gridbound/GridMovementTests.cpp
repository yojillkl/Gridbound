#include "GridGame.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridMovementTest,"Gridbound.Movement.JumpAndEightDirections",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridMovementTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Opposite keys cancel"),GridRules::MovementInput(true,true,true,true),FIntPoint::ZeroValue);
    TestEqual(TEXT("Forward right combination"),GridRules::MovementInput(true,false,false,true),FIntPoint(1,1));
    TestEqual(TEXT("Forward left combination"),GridRules::MovementInput(true,false,true,false),FIntPoint(1,-1));
    TestEqual(TEXT("Forward at 45 degrees walks diagonally"),GridRules::MoveDirection(45,FIntPoint(1,0)),FIntPoint(1,1));
    TestEqual(TEXT("Forward right at 45 degrees rotates with camera"),GridRules::MoveDirection(45,FIntPoint(1,1)),FIntPoint(0,1));
    TestEqual(TEXT("Back at 90 degrees"),GridRules::MoveDirection(90,FIntPoint(-1,0)),FIntPoint(0,-1));
    for(int32 Yaw=0;Yaw<360;++Yaw)
    {
        const FIntPoint Step=GridRules::MoveDirection(Yaw,FIntPoint(1,0));
        TestTrue(TEXT("Each result is one of eight neighbours"),FMath::Max(FMath::Abs(Step.X),FMath::Abs(Step.Y))==1);
        const FVector Actual=FVector(Step.X,Step.Y,0).GetSafeNormal();
        TestTrue(TEXT("Movement follows camera within 22.5 degrees"),FVector::DotProduct(Actual,FRotator(0,Yaw,0).Vector())>=FMath::Cos(PI/8)-.0001f);
    }
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("World"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Pawn=World->SpawnActor<AGridPawn>();
    if(!TestNotNull(TEXT("Pawn"),Pawn)) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false; }
    const auto Reset=[Pawn]()
    {
        Pawn->ResetArena(); Pawn->CurrentCell=FIntPoint(3,3); Pawn->Destination=Pawn->CurrentCell;
        Pawn->SetActorLocation(GridRules::Center(Pawn->CurrentCell,GridRules::ActorOriginHeight));
    };
    Reset();
    TestTrue(TEXT("Grounded at spawn"),Pawn->IsGrounded());
    TestTrue(TEXT("Can jump from ground"),Pawn->TryJump());
    TestFalse(TEXT("Cannot double jump before first vertical tick"),Pawn->TryJump());
    Pawn->UpdateElevation(.41f);
    TestTrue(TEXT("Jump apex is 145 cm"),FMath::IsNearlyEqual(Pawn->FootHeight,145.f,.01f));
    TestTrue(TEXT("First person eye follows jump"),FMath::IsNearlyEqual(Pawn->Camera->GetComponentLocation().Z,275.,.01));
    TestFalse(TEXT("Cannot double jump at apex"),Pawn->TryJump());
    Pawn->UpdateElevation(.39f);
    TestFalse(TEXT("Jump is still airborne at 0.80 seconds"),Pawn->IsGrounded());
    Pawn->UpdateElevation(.021f);
    TestTrue(TEXT("Jump returns to ground"),Pawn->IsGrounded());
    TestTrue(TEXT("Landing clamps to surface"),FMath::IsNearlyZero(Pawn->FootHeight));
    TestTrue(TEXT("Can jump again after landing"),Pawn->TryJump());
    Reset();
    TestTrue(TEXT("Diagonal movement starts"),Pawn->TryStartMove(FIntPoint(1,1),0));
    const FIntPoint LockedDestination=Pawn->Destination;
    TestFalse(TEXT("Turning cannot bend the current grid step"),Pawn->TryStartMove(FIntPoint(1,0),180));
    TestEqual(TEXT("Current destination stays fixed"),Pawn->Destination,LockedDestination);
    Pawn->AdvanceMovement(.3f);
    TestTrue(TEXT("Diagonal does not finish in cardinal time"),Pawn->bMoving);
    TestTrue(TEXT("Diagonal distance travelled equals cardinal distance"),FMath::IsNearlyEqual(FVector::Dist2D(Pawn->GetActorLocation(),GridRules::Center(FIntPoint(3,3))),150.,.01));
    Pawn->AdvanceMovement(.13f);
    TestEqual(TEXT("Diagonal arrives at neighbour"),Pawn->CurrentCell,FIntPoint(4,4));
    TestFalse(TEXT("Diagonal ends on exact cell center"),Pawn->bMoving);
    TestTrue(TEXT("Position remains centered"),Pawn->GetActorLocation().Equals(GridRules::Center(FIntPoint(4,4),GridRules::ActorOriginHeight),.01));
    Reset(); Pawn->MakeMud(Pawn->CurrentCell);
    Pawn->TryStartMove(FIntPoint(1,1),0); Pawn->AdvanceMovement(.6f);
    TestTrue(TEXT("Mud also slows diagonals"),Pawn->bMoving);
    Pawn->AdvanceMovement(.25f);
    TestFalse(TEXT("Muddy diagonal completes in about 0.849 seconds"),Pawn->bMoving);
    Reset(); Pawn->MakeMud(Pawn->CurrentCell);
    Pawn->TryStartMove(FIntPoint(1,1),0);
    TestTrue(TEXT("Can jump while moving from mud"),Pawn->TryJump());
    Pawn->UpdateElevation(.1f); Pawn->AdvanceMovement(.1f);
    TestTrue(TEXT("Jump and horizontal movement coexist"),Pawn->FootHeight>0 && Pawn->GetActorLocation().X>450);
    TestEqual(TEXT("Airborne character is not slowed by mud"),Pawn->MovementSpeedMultiplier(Pawn->CurrentCell,Pawn->FootHeight),1.f);
    Reset();
    Pawn->PlaceWall(FIntPoint(4,3)); Pawn->TickWalls(.4f); Pawn->UpdateElevation(.4f);
    for(int32 I=Pawn->Walls.Num()-1;I>=0;--I) if(Pawn->Walls[I].Cell!=FIntPoint(4,3)) Pawn->RemoveWall(I);
    TestTrue(TEXT("Diagonal destination itself is clear"),Pawn->CanEnter(FIntPoint(4,4)));
    TestFalse(TEXT("Diagonal cannot clip wall corner"),Pawn->CanStep(FIntPoint(1,1)));
    TestFalse(TEXT("Jump does not bypass tall wall"),Pawn->TryStartMove(FIntPoint(1,0),0));
    Reset();
    Pawn->PlaceWall(Pawn->CurrentCell); Pawn->TickWalls(.4f); Pawn->UpdateElevation(.4f);
    TestTrue(TEXT("Can jump from wall top"),Pawn->TryJump());
    Pawn->UpdateElevation(.41f);
    TestTrue(TEXT("Wall-top jump is relative to support height"),FMath::IsNearlyEqual(Pawn->FootHeight,595.f,.01f));
    Pawn->UpdateElevation(1.f);
    TestTrue(TEXT("Lands on intact wall"),Pawn->IsGrounded() && FMath::IsNearlyEqual(Pawn->FootHeight,450.f));
    Pawn->TryJump(); Pawn->UpdateElevation(.2f);
    for(int32 I=Pawn->Walls.Num()-1;I>=0;--I) Pawn->RemoveWall(I);
    Pawn->UpdateElevation(2.f);
    TestTrue(TEXT("Destroyed support during jump lands on floor"),Pawn->IsGrounded() && FMath::IsNearlyZero(Pawn->FootHeight));
    Reset(); Pawn->FootHeight=200; Pawn->FallSpeed=50;
    Pawn->SetActorLocation(GridRules::Center(Pawn->CurrentCell,275));
    TestFalse(TEXT("Cannot jump during a fall"),Pawn->TryJump());
    Reset(); Pawn->TryJump(); Pawn->UpdateElevation(.1f); Pawn->ResetArena();
    TestFalse(TEXT("Reset clears jumping state"),Pawn->bJumping);
    TestTrue(TEXT("Reset clears vertical velocity"),FMath::IsNearlyZero(Pawn->FallSpeed));
    Pawn->Health=0;
    TestFalse(TEXT("Defeated character cannot jump"),Pawn->TryJump());
    TestFalse(TEXT("Defeated character cannot walk"),Pawn->TryStartMove(FIntPoint(1,0),0));
    World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return true;
}
#endif
