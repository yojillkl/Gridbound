#include "GridGame.h"
#include "GridArt.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridEnvironmentTest,"Gridbound.Combat.WallSupportBurningAndDemolition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridEnvironmentTest::RunTest(const FString& Parameters)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("World"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Pawn=World->SpawnActor<AGridPawn>();
    if(!TestNotNull(TEXT("Pawn"),Pawn)) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false; }
    Pawn->CurrentCell=FIntPoint(3,3); Pawn->Destination=Pawn->CurrentCell;
    Pawn->SetActorLocation(GridRules::Center(Pawn->CurrentCell,GridRules::ActorOriginHeight));
    TestTrue(TEXT("Camera attaches directly to pawn"),Pawn->Camera->GetAttachParent()==Pawn->GetRootComponent());
    TestTrue(TEXT("Camera starts at eye height"),FMath::IsNearlyEqual(Pawn->Camera->GetComponentLocation().Z,130.));
    AGridPawn::FTarget Target; Target.Cell=FIntPoint(3,4);
    Target.Actor=Pawn->MakeBlock(GridRules::Center(Target.Cell,GridRules::ActorOriginHeight),FVector(.65f,.65f,1.5f),FLinearColor::Red);
    Pawn->Targets.Add(Target);
    TestTrue(TEXT("Wall allowed under player and enemy"),Pawn->PlaceWall(Pawn->CurrentCell));
    Pawn->TickWalls(.175f); Pawn->UpdateElevation(.175f);
    TestTrue(TEXT("Player follows rising wall"),FMath::IsNearlyEqual(Pawn->FootHeight,225.f));
    TestTrue(TEXT("Enemy follows rising wall"),FMath::IsNearlyEqual(Pawn->Targets[0].FootHeight,225.f));
    Pawn->TickWalls(.325f); Pawn->UpdateElevation(.325f);
    TestTrue(TEXT("Player reaches wall top"),FMath::IsNearlyEqual(Pawn->FootHeight,450.f));
    TestTrue(TEXT("First person camera follows lift"),FMath::IsNearlyEqual(Pawn->Camera->GetComponentLocation().Z,580.));
    TestTrue(TEXT("Walking along unoccupied wall top allowed"),Pawn->CanEnter(FIntPoint(3,2)));
    int32 CenterIndex=INDEX_NONE;
    for(int32 I=0;I<Pawn->Walls.Num();++I) if(Pawn->Walls[I].Cell==Pawn->CurrentCell) CenterIndex=I;
    TestTrue(TEXT("Center pillar exists"),CenterIndex!=INDEX_NONE);
    AActor* CenterActor=Pawn->Walls[CenterIndex].Actor.Get();
    Pawn->ResolveFireballImpact(CenterActor,CenterActor->GetActorLocation());
    TestEqual(TEXT("Fireball demolition removes 50 durability"),Pawn->Walls[CenterIndex].Durability,50);
    TestEqual(TEXT("Demolition does not damage character on top"),Pawn->Health,100);
    for(int32 I=0;I<Pawn->Walls.Num();++I) if(I!=CenterIndex)
        TestEqual(TEXT("Other pillar durability is independent"),Pawn->Walls[I].Durability,100);
    Pawn->ResolveFireballImpact(Pawn->Targets[0].Actor.Get(),Pawn->Targets[0].Actor->GetActorLocation());
    TestEqual(TEXT("Character impact deals 50 HP"),Pawn->Targets[0].Health,50);
    TestEqual(TEXT("Character impact never applies demolition below them"),Pawn->Walls[CenterIndex].Durability,50);
    Pawn->ResolveFireballImpact(CenterActor,CenterActor->GetActorLocation());
    TestEqual(TEXT("Only one destroyed pillar is removed"),Pawn->Walls.Num(),4);
    Pawn->UpdateElevation(.1f);
    TestTrue(TEXT("Unsupported player begins falling"),Pawn->FootHeight>0 && Pawn->FootHeight<450);
    TestTrue(TEXT("Enemy stays on neighbouring intact pillar"),FMath::IsNearlyEqual(Pawn->Targets[0].FootHeight,450.f));
    Pawn->UpdateElevation(1.f);
    TestTrue(TEXT("Player lands on ground"),FMath::IsNearlyZero(Pawn->FootHeight));
    Pawn->TickWalls(59.49f);
    TestEqual(TEXT("Pillars still exist before 60 seconds"),Pawn->Walls.Num(),4);
    Pawn->TickWalls(.02f); Pawn->UpdateElevation(1.f);
    TestEqual(TEXT("Pillars expire at 60 seconds"),Pawn->Walls.Num(),0);
    TestTrue(TEXT("Enemy lands when its pillar expires"),FMath::IsNearlyZero(Pawn->Targets[0].FootHeight));

    // Real terrain collision, rather than calling the ignition helper alone.
    AActor* Grass=GridArt::CreateTile(World,FIntPoint(4,3),Pawn->BaseMaterial);
    Pawn->TerrainTiles.Add(FIntPoint(4,3),Grass);
    Pawn->LaunchFireball((GridRules::Center(FIntPoint(4,3))-Pawn->SpellOrigin()).GetSafeNormal());
    Pawn->TickFireballs(1.f);
    TestEqual(TEXT("Fireball ground impact ignites grass"),Pawn->BurningCells.Num(),1);
    TestFalse(TEXT("River cannot ignite"),Pawn->IgniteCell(FIntPoint(10,6)));
    TestFalse(TEXT("Gravel cannot ignite"),Pawn->IgniteCell(FIntPoint(3,10)));
    TestFalse(TEXT("Outside board cannot ignite"),Pawn->IgniteCell(FIntPoint(-1,0)));
    Pawn->CurrentCell=FIntPoint(4,3); Pawn->Destination=Pawn->CurrentCell;
    Pawn->SetActorLocation(GridRules::Center(Pawn->CurrentCell,GridRules::ActorOriginHeight));
    Pawn->Targets[0].Cell=Pawn->CurrentCell; Pawn->Targets[0].Health=100;
    Pawn->Targets[0].Actor->SetActorLocation(GridRules::Center(Pawn->CurrentCell,GridRules::ActorOriginHeight));
    for(int32 Frame=0;Frame<60;++Frame) Pawn->TickBurning(1.f/60.f);
    TestEqual(TEXT("60 frames burn player for exactly 10 HP"),Pawn->Health,90);
    TestEqual(TEXT("Burn also affects enemy"),Pawn->Targets[0].Health,90);
    Pawn->IgniteCell(Pawn->CurrentCell); Pawn->IgniteCell(Pawn->CurrentCell);
    TestEqual(TEXT("Reignition never stacks patches"),Pawn->BurningCells.Num(),1);
    Pawn->TickBurning(1.f);
    TestEqual(TEXT("Reignition does not stack DPS"),Pawn->Health,80);
    TestTrue(TEXT("Wall can raise characters out of fire"),Pawn->PlaceWall(Pawn->CurrentCell));
    Pawn->TickWalls(.4f); Pawn->UpdateElevation(.4f); Pawn->TickBurning(1.f);
    TestEqual(TEXT("Player on wall does not burn"),Pawn->Health,80);
    TestEqual(TEXT("Enemy on wall does not burn"),Pawn->Targets[0].Health,80);
    Pawn->TickBurning(20.f);
    TestTrue(TEXT("Flames expire after ten seconds"),FMath::IsNearlyZero(Pawn->BurningCells[0].Remaining));
    for(int32 I=Pawn->Walls.Num()-1;I>=0;--I) Pawn->RemoveWall(I);
    Pawn->UpdateElevation(2.f); Pawn->TickBurning(1.f);
    TestEqual(TEXT("Extinguished ground deals no damage"),Pawn->Health,80);
    Pawn->IgniteCell(Pawn->CurrentCell); Pawn->TickBurning(.5f);
    TestEqual(TEXT("Burned terrain can reignite"),Pawn->Health,75);
    Pawn->CurrentCell=FIntPoint(5,3); Pawn->SetActorLocation(GridRules::Center(Pawn->CurrentCell,GridRules::ActorOriginHeight));
    Pawn->TickBurning(1.f);
    TestEqual(TEXT("Leaving burning tile stops damage"),Pawn->Health,75);
    // Wall construction during a step must settle and lift instead of trapping the player.
    Pawn->bMoving=true; Pawn->Destination=FIntPoint(6,3);
    Pawn->SetActorLocation(GridRules::Center(FIntPoint(6,3),GridRules::ActorOriginHeight)-FVector(30,0,0));
    TestTrue(TEXT("Wall can intersect moving destination"),Pawn->PlaceWall(FIntPoint(6,3)));
    Pawn->TickWalls(.4f); Pawn->UpdateElevation(.4f);
    TestFalse(TEXT("Intersected step safely settles"),Pawn->bMoving);
    TestEqual(TEXT("Step settles to physical cell"),Pawn->CurrentCell,FIntPoint(6,3));
    TestTrue(TEXT("Moving player is supported above wall"),FMath::IsNearlyEqual(Pawn->FootHeight,450.f));
    Pawn->ResetArena();
    TestEqual(TEXT("Reset removes burning and charred patches"),Pawn->BurningCells.Num(),0);
    TestEqual(TEXT("Reset removes walls"),Pawn->Walls.Num(),0);
    TestTrue(TEXT("Reset restores player elevation"),FMath::IsNearlyZero(Pawn->FootHeight));
    TestEqual(TEXT("Reset restores health"),Pawn->Health,100);
    World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return true;
}
#endif
