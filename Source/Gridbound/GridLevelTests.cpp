#include "GridGame.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/DamageEvents.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridLevelTest,"Gridbound.Level.ElementalCrossing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridLevelTest::RunTest(const FString& Parameters)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("World"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* P=World->SpawnActor<AGridPawn>();
    if(!TestNotNull(TEXT("Pawn"),P)) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false; }
    P->bLevelMode=true; P->ResetArena();
    auto Clear=[P]()
    {
        for(auto& T:P->Targets) if(T.Health>0 && T.Actor.IsValid())
        {
            AActor* A=T.Actor.Get();
            P->ResolveFireballImpact(A,A->GetActorLocation());
            P->ResolveFireballImpact(A,A->GetActorLocation());
        }
    };
    TestEqual(TEXT("Single map has ten persistent guards"),P->Targets.Num(),10);
    TestFalse(TEXT("Spawn is outside initial sight"),P->EnemySeesPlayer(P->Targets[0]));
    TestFalse(TEXT("Old camp gate removed"),P->LevelBlocked(FIntPoint(7,10)));
    TestFalse(TEXT("Old summit gate removed"),P->LevelBlocked(FIntPoint(13,10)));
    TestTrue(TEXT("River still blocks walking"),P->LevelBlocked(FIntPoint(10,6)));
    TestFalse(TEXT("Second crossing is open"),P->LevelBlocked(FIntPoint(11,21)));
    TestTrue(TEXT("Boundary blocks movement"),P->LevelBlocked(FIntPoint(31,10)));
    // Flood fill permits jumpable rises and proves both crossings independently reach the relic.
    for(int32 ClosedCrossing:{10,21})
    {
        TSet<FIntPoint> Visited; TArray<FIntPoint> Queue; Queue.Add(P->PlayerSpawnCell); Visited.Add(Queue[0]);
        for(int32 Q=0;Q<Queue.Num();++Q) for(const FIntPoint D:{FIntPoint(1,0),FIntPoint(-1,0),FIntPoint(0,1),FIntPoint(0,-1)})
        {
            const FIntPoint C=Queue[Q]+D;
            if(Visited.Contains(C) || P->LevelBlocked(C) || (C.X>=9 && C.X<=12 && FMath::Abs(C.Y-ClosedCrossing)<=1)) continue;
            if(P->LevelHeight(C)>P->LevelHeight(Queue[Q])+GridRules::JumpHeight) continue;
            Queue.Add(C); Visited.Add(C);
        }
        TestTrue(TEXT("Independent crossing reaches relic staircase"),Visited.Contains(FIntPoint(26,16)));
    }
    P->RespawnPlayer(FIntPoint(26,16)); P->UpdateElevation(.1f); P->InteractLevel();
    TestTrue(TEXT("Can take relic without killing guards"),P->bRelicCarried);
    TestFalse(TEXT("Pickup alone does not win"),P->bLevelComplete);
    TestTrue(TEXT("Nearby guards hear relic alarm"),P->Targets.Last().AlertTime>0);
    TestEqual(TEXT("Distant guards are not omniscient"),P->Targets[0].AlertTime,0.f);
    P->TakeDamage(100,FDamageEvent(),nullptr,nullptr); P->TickLevel(.1f);
    TestFalse(TEXT("Death drops carried relic"),P->bRelicCarried);
    TestEqual(TEXT("Relic remains at death location"),P->RelicCell,FIntPoint(26,16));
    P->TickCombatants(2.1f);
    TestEqual(TEXT("Death returns to camp"),P->CurrentCell,P->PlayerSpawnCell);
    P->RespawnPlayer(FIntPoint(25,16)); P->UpdateElevation(.1f); P->InteractLevel();
    TestTrue(TEXT("Dropped relic can be recovered"),P->bRelicCarried);
    P->RespawnPlayer(P->PlayerSpawnCell); P->InteractLevel();
    TestTrue(TEXT("Returning relic wins with guards alive"),P->bLevelComplete);
    P->ResetArena(); Clear(); P->Targets.Reset(); P->RespawnPlayer(FIntPoint(25,14));
    TestFalse(TEXT("Cannot walk straight onto high ruin"),P->CanEnter(FIntPoint(26,14)));
    TestTrue(TEXT("Wall provides alternate ascent"),P->PlaceWall(FIntPoint(25,14)));
    P->TickWalls(.4f); P->UpdateElevation(.4f);
    TestTrue(TEXT("Wall lifts player above ruin"),FMath::IsNearlyEqual(P->FootHeight,450.f));
    TestTrue(TEXT("Can step from wall onto ruin"),P->TryStartMove(FIntPoint(1,0),0));
    P->AdvanceMovement(.31f); P->UpdateElevation(1.f);
    TestTrue(TEXT("Land on ruin at 300 cm"),FMath::IsNearlyEqual(P->FootHeight,300.f));
    P->ResetArena();
    TestFalse(TEXT("Restart clears relic and completion"),P->bRelicCarried || P->bLevelComplete);
    TestEqual(TEXT("Restart clears walls"),P->Walls.Num(),0);

    // Windup commits to a cell, so moving out before impact is a real dodge.
    Clear(); P->Targets.Reset(); P->RespawnPlayer(FIntPoint(3,3)); P->SpawnWarrior(FIntPoint(4,3));
    P->Targets[0].AlertTime=8; P->Targets[0].AttackCooldown=0; P->TickCombatants(.01f);
    TestTrue(TEXT("Melee telegraphs before damage"),P->Targets[0].Windup>0);
    TestEqual(TEXT("Telegraph is not instant damage"),P->Health,100);
    P->CurrentCell=FIntPoint(3,2); P->SetActorLocation(GridRules::Center(P->CurrentCell,75));
    P->TickCombatants(.46f); TestEqual(TEXT("Leaving marked cell dodges slash"),P->Health,100);
    P->RespawnPlayer(FIntPoint(3,3)); P->Targets[0].ThinkTime=0; P->Targets[0].AttackCooldown=0;
    P->TickCombatants(.01f); P->TickCombatants(.46f);
    TestEqual(TEXT("Staying in marked cell takes 20 damage"),P->Health,80);
    P->IgniteCell(FIntPoint(4,3)); P->Targets[0].ThinkTime=0;
    P->TickCombatants(.1f);
    TestTrue(TEXT("Burning guard attempts to leave flames"),P->Targets[0].bWalking);
    TestTrue(TEXT("Escape destination differs from burning cell"),P->Targets[0].MoveDestination!=FIntPoint(4,3));
    P->TakeDamage(100,FDamageEvent(),nullptr,nullptr); P->TickCombatants(2.01f);
    TestEqual(TEXT("Checkpoint respawn restores player"),P->Health,100);
    TestEqual(TEXT("Level tracks deaths"),P->LevelDeaths,1);
    World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return true;
}
#endif
