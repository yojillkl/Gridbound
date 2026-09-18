#include "GridGame.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/DamageEvents.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridCombatTest,"Gridbound.Combat.FireballAndEarthWall",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridCombatTest::RunTest(const FString& Parameters)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("Test world"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    AGridPawn* Pawn=World->SpawnActor<AGridPawn>();
    if(!TestNotNull(TEXT("Player"),Pawn)) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false; }
    Pawn->SetActorLocation(GridRules::Center(Pawn->CurrentCell,GridRules::ActorOriginHeight));
    TestEqual(TEXT("Player starts with 100 HP"),Pawn->Health,100);
    TestEqual(TEXT("No spell selected at startup"),Pawn->SelectedSkill,INDEX_NONE);
    Pawn->CastSkill(INDEX_NONE);
    TestEqual(TEXT("Unselected cast creates no projectile"),Pawn->Fireballs.Num(),0);

    const FDamageEvent DamageEvent;
    TestEqual(TEXT("Player accepts damage"),Pawn->TakeDamage(50,DamageEvent,nullptr,nullptr),50.f);
    TestEqual(TEXT("Player health after hit"),Pawn->Health,50);
    Pawn->TakeDamage(-20,DamageEvent,nullptr,nullptr);
    TestEqual(TEXT("Negative damage cannot heal"),Pawn->Health,50);
    Pawn->TakeDamage(200,DamageEvent,nullptr,nullptr);
    TestEqual(TEXT("Health clamps to zero"),Pawn->Health,0);
    Pawn->Health=100;

    auto AddTarget=[&](FIntPoint Cell)
    {
        AGridPawn::FTarget Target; Target.Cell=Cell;
        Target.Actor=Pawn->MakeBlock(GridRules::Center(Cell,GridRules::ActorOriginHeight),FVector(.65f,.65f,1.5f),FLinearColor::Red);
        Pawn->Targets.Add(Target);
    };
    AddTarget(FIntPoint(6,10)); AddTarget(FIntPoint(8,10));
    Pawn->LaunchFireball(FVector::ForwardVector);
    TestEqual(TEXT("Projectile does not deal instant damage"),Pawn->Targets[0].Health,100);
    Pawn->TickFireballs(.05f);
    TestEqual(TEXT("Projectile takes time to reach target"),Pawn->Targets[0].Health,100);
    Pawn->TickFireballs(1.f);
    TestEqual(TEXT("Swept hit deals 50 even on a long frame"),Pawn->Targets[0].Health,50);
    TestEqual(TEXT("Fireball cannot penetrate first enemy"),Pawn->Targets[1].Health,100);
    TestEqual(TEXT("Projectile disappears on hit"),Pawn->Fireballs.Num(),0);
    Pawn->LaunchFireball(FVector::ForwardVector); Pawn->TickFireballs(1.f);
    TestEqual(TEXT("Two fireballs kill a 100 HP enemy"),Pawn->Targets[0].Health,0);
    TestTrue(TEXT("Defeated enemy no longer blocks movement"),Pawn->CanEnter(FIntPoint(6,10)));

    TestTrue(TEXT("Wall can be placed under player"),Pawn->CanPlaceWall(Pawn->CurrentCell));
    Pawn->bWallAlongX=true;
    TestEqual(TEXT("Wall clips to three cells at board edge"),Pawn->PlaceableWallCells(FIntPoint(0,10)).Num(),3);
    Pawn->bWallAlongX=false;
    TestFalse(TEXT("Wall center cannot exceed ten cells"),Pawn->CanPlaceWall(FIntPoint(14,10)));
    TestTrue(TEXT("Wall can be placed under living enemy"),Pawn->CanPlaceWall(FIntPoint(8,10)));
    Pawn->bMoving=true; Pawn->Destination=FIntPoint(4,10);
    TestTrue(TEXT("Wall accepts moving destination for safe lifting"),Pawn->CanPlaceWall(FIntPoint(4,10)));
    Pawn->bMoving=false;
    TestTrue(TEXT("Place 1 x 5 wall"),Pawn->PlaceWall(FIntPoint(5,10)));
    Pawn->TickWalls(.4f); Pawn->UpdateElevation(.4f);
    TestEqual(TEXT("Wall occupies exactly five cells"),Pawn->Walls.Num(),5);
    for(int32 Y=8;Y<=12;++Y) TestFalse(TEXT("Every wall cell blocks movement"),Pawn->CanEnter(FIntPoint(5,Y)));
    TestTrue(TEXT("Adjacent free cell remains walkable"),Pawn->CanEnter(FIntPoint(4,10)));
    TestFalse(TEXT("Overlapping wall rejected"),Pawn->PlaceWall(FIntPoint(5,10)));
    TestEqual(TEXT("Invalid wall creates no partial segments"),Pawn->Walls.Num(),5);
    TestFalse(TEXT("Wall blocks caster line of sight"),Pawn->HasLineOfSight(FIntPoint(8,10),Pawn->Targets[1].Actor.Get()));
    Pawn->LaunchFireball(FVector::ForwardVector); Pawn->TickFireballs(1.f);
    TestEqual(TEXT("Wall stops fireball before enemy"),Pawn->Targets[1].Health,100);
    TestEqual(TEXT("Wall impact destroys projectile"),Pawn->Fireballs.Num(),0);
    if(Pawn->Walls.Num())
    {
        FVector Origin,Extent; Pawn->Walls[0].Actor->GetActorBounds(false,Origin,Extent);
        TestTrue(TEXT("Wall is three cells high"),FMath::IsNearlyEqual(Extent.Z*2,450.f));
    }
    for(auto& Wall:Pawn->Walls) if(Wall.Actor.IsValid()) Wall.Actor->Destroy();
    Pawn->Walls.Reset(); Pawn->bWallAlongX=true;
    TestTrue(TEXT("Place rotated 5 x 1 wall"),Pawn->PlaceWall(FIntPoint(3,13)));
    Pawn->TickWalls(.4f); Pawn->UpdateElevation(.4f);
    for(int32 X=1;X<=5;++X) TestFalse(TEXT("Rotated wall occupies X axis"),Pawn->CanEnter(FIntPoint(X,13)));
    Pawn->LaunchFireball(-FVector::ForwardVector); Pawn->TickFireballs(1.f);
    TestEqual(TEXT("Missed fireball expires at max range"),Pawn->Fireballs.Num(),0);
    Pawn->Health=10; Pawn->SelectedSkill=1; Pawn->Cooldowns[0]=.6f;
    Pawn->LaunchFireball(FVector::ForwardVector);
    Pawn->ResetArena();
    TestEqual(TEXT("Reset restores player health"),Pawn->Health,100);
    TestEqual(TEXT("Reset clears walls"),Pawn->Walls.Num(),0);
    TestEqual(TEXT("Reset clears projectiles"),Pawn->Fireballs.Num(),0);
    TestEqual(TEXT("Reset clears selection"),Pawn->SelectedSkill,INDEX_NONE);
    TestEqual(TEXT("Reset clears cooldown"),Pawn->Cooldowns[0],0.f);
    TestEqual(TEXT("Reset respawns three enemies"),Pawn->Targets.Num(),3);
    for(const auto& Target:Pawn->Targets) TestEqual(TEXT("Reset restores enemy HP"),Target.Health,100);
    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
    return true;
}
#endif
