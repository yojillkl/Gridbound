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
    TestEqual(TEXT("Camp starts with two guards"),P->Targets.Num(),2);
    TestFalse(TEXT("Guard cannot see spawn across entire map"),P->EnemySeesPlayer(P->Targets[0]));
    TestTrue(TEXT("First gate is locked"),P->LevelBlocked(FIntPoint(7,10)));
    TestTrue(TEXT("River cannot be walked through"),P->LevelBlocked(FIntPoint(10,6)));
    TestTrue(TEXT("Map boundary blocks movement"),P->LevelBlocked(FIntPoint(0,10)));
    TestFalse(TEXT("Wall cannot cover permanent cliff"),P->PlaceableWallCells(FIntPoint(7,6)).Contains(FIntPoint(7,6)));
    P->RespawnPlayer(FIntPoint(4,10)); P->InteractLevel();
    TestEqual(TEXT("Living guards block capture"),P->LevelStage,0);
    Clear(); P->TickCombatants(.1f);
    TestEqual(TEXT("Authored encounter does not refill forever"),P->Targets.Num(),2);
    P->InteractLevel(); TestEqual(TEXT("Unlit fire seal blocks capture"),P->LevelStage,0);
    P->ResolveFireballImpact(P->LevelBeacons[0].Get(),FVector::ZeroVector);
    P->InteractLevel();
    TestEqual(TEXT("Fire seal advances to crossing"),P->LevelStage,1);
    TestEqual(TEXT("Checkpoint moves to river bank"),P->PlayerSpawnCell,FIntPoint(8,10));
    TestFalse(TEXT("Camp gate opens"),P->LevelBlocked(FIntPoint(7,10)));
    TestTrue(TEXT("Summit gate stays locked"),P->LevelBlocked(FIntPoint(13,10)));
    TestEqual(TEXT("Crossing has three guards"),P->Targets.Num(),3);
    P->TickLevel(.1f);
    TestEqual(TEXT("Crossing starts with nine burning grass cells"),P->BurningCells.Num(),9);
    TestTrue(TEXT("Rain can cool crossing from checkpoint"),P->CastRain(FIntPoint(11,10)));
    TestTrue(TEXT("Rain seal registers coverage"),P->bRainSeal);
    for(const auto& B:P->BurningCells) TestTrue(TEXT("Rain extinguishes crossing"),B.Remaining<=0);
    TestEqual(TEXT("Extinguished crossing slows grounded characters"),P->MovementSpeedMultiplier(FIntPoint(10,10),0),.5f);
    Clear(); P->RespawnPlayer(FIntPoint(12,9)); P->InteractLevel();
    TestEqual(TEXT("Rain seal advances to summit"),P->LevelStage,2);
    TestFalse(TEXT("Final gate opens"),P->LevelBlocked(FIntPoint(13,10)));
    TestEqual(TEXT("Final encounter has four guards"),P->Targets.Num(),4);
    Clear(); P->RespawnPlayer(FIntPoint(16,10));
    TestFalse(TEXT("Cannot walk directly up two-cell platform"),P->CanEnter(FIntPoint(17,10)));
    TestTrue(TEXT("Earth wall can raise player beside platform"),P->PlaceWall(FIntPoint(16,10)));
    P->TickWalls(.4f); P->UpdateElevation(.4f);
    TestTrue(TEXT("Wall reaches 450 cm"),FMath::IsNearlyEqual(P->FootHeight,450.f));
    TestTrue(TEXT("Raised player can step onto summit"),P->TryStartMove(FIntPoint(1,0),0));
    P->AdvanceMovement(.31f); P->UpdateElevation(1.f);
    TestTrue(TEXT("Player lands on permanent 300 cm platform"),FMath::IsNearlyEqual(P->FootHeight,300.f));
    P->InteractLevel(); TestTrue(TEXT("All three encounters can be completed"),P->bLevelComplete);
    const float CompleteTime=P->LevelSeconds; P->TickLevel(1.f);
    TestEqual(TEXT("Completion timer freezes"),P->LevelSeconds,CompleteTime);
    P->TakeDamage(100,FDamageEvent(),nullptr,nullptr);
    TestEqual(TEXT("Completion protects player from leftover effects"),P->Health,100);
    P->ResetArena();
    TestFalse(TEXT("Restart clears completion"),P->bLevelComplete);
    TestEqual(TEXT("Restart resets stage"),P->LevelStage,0);
    TestEqual(TEXT("Restart clears old walls"),P->Walls.Num(),0);
    TestEqual(TEXT("Restart removes mud"),P->MuddyCells.Num(),0);

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
