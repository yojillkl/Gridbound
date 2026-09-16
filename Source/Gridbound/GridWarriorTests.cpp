#include "GridGame.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/DamageEvents.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGridWarriorTest,"Gridbound.Combat.WarriorsAndRespawning",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGridWarriorTest::RunTest(const FString& Parameters)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("World"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* Pawn=World->SpawnActor<AGridPawn>();
    if(!TestNotNull(TEXT("Pawn"),Pawn)) { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false; }
    auto Setup=[&](FIntPoint Player,FIntPoint Enemy)
    {
        Pawn->ResetArena();
        for(auto& T:Pawn->Targets) if(T.Actor.IsValid()) T.Actor->Destroy();
        Pawn->Targets.Reset();
        Pawn->RespawnPlayer(Player); Pawn->SpawnWarrior(Enemy);
    };
    Setup(FIntPoint(3,3),FIntPoint(4,3));
    Pawn->TickCombatants(.5f);
    TestEqual(TEXT("Spawn gives one second before first attack"),Pawn->Health,100);
    Pawn->TickCombatants(.5f);
    TestEqual(TEXT("Slash damages player by 20"),Pawn->Health,80);
    Pawn->TickCombatants(.99f);
    TestEqual(TEXT("Slash cannot repeat before one second"),Pawn->Health,80);
    Pawn->TickCombatants(.011f);
    TestEqual(TEXT("Slash repeats after cooldown"),Pawn->Health,60);
    TestEqual(TEXT("Slash does not damage attacker"),Pawn->Targets[0].Health,100);
    Pawn->FootHeight=145; Pawn->SetActorLocation(GridRules::Center(Pawn->CurrentCell,220));
    Pawn->TickCombatants(1.f);
    TestEqual(TEXT("Jump apex is outside melee vertical reach"),Pawn->Health,60);

    Setup(FIntPoint(3,3),FIntPoint(7,3));
    const float StartDistance=FVector::Dist2D(Pawn->GetActorLocation(),Pawn->Targets[0].Actor->GetActorLocation());
    for(int32 Frame=0;Frame<120;++Frame) { Pawn->UpdateElevation(1.f/60.f); Pawn->TickCombatants(1.f/60.f); }
    TestTrue(TEXT("Warrior approaches player"),FVector::Dist2D(Pawn->GetActorLocation(),Pawn->Targets[0].Actor->GetActorLocation())<StartDistance);
    TestTrue(TEXT("Warrior stops outside player cell"),Pawn->Targets[0].Cell!=Pawn->CurrentCell);
    TestTrue(TEXT("Pursuit ends in melee damage"),Pawn->Health<100);

    Setup(FIntPoint(3,3),FIntPoint(7,3));
    Pawn->TickCombatants(0.f);
    TestTrue(TEXT("Walking reserves destination"),Pawn->Targets[0].bWalking);
    TestFalse(TEXT("Player cannot enter enemy reserved cell"),Pawn->CanEnter(Pawn->Targets[0].MoveDestination));
    Pawn->MakeMud(Pawn->Targets[0].Cell);
    Pawn->TickCombatants(.45f);
    TestTrue(TEXT("Mud doubles warrior step time"),Pawn->Targets[0].bWalking && Pawn->Targets[0].Cell==FIntPoint(7,3));

    Setup(FIntPoint(3,3),FIntPoint(4,3));
    TestTrue(TEXT("Place support beneath player"),Pawn->PlaceWall(Pawn->CurrentCell));
    Pawn->TickWalls(.4f); Pawn->UpdateElevation(.4f);
    int32 Center=INDEX_NONE;
    for(int32 I=0;I<Pawn->Walls.Num();++I) if(Pawn->Walls[I].Cell==Pawn->CurrentCell) Center=I;
    TestTrue(TEXT("Support pillar exists"),Center!=INDEX_NONE);
    Pawn->Targets[0].AttackCooldown=0;
    TestFalse(TEXT("Warrior cannot hit player through tall wall"),Pawn->TryWarriorSlash(0));
    Pawn->TickCombatants(0.f);
    TestEqual(TEXT("AI demolishes blocking pillar by 10"),Pawn->Walls[Center].Durability,90);
    TestEqual(TEXT("Demolition never subtracts player HP"),Pawn->Health,100);
    for(int32 I=0;I<Pawn->Walls.Num();++I) if(I!=Center)
        TestEqual(TEXT("Slash only demolishes one pillar"),Pawn->Walls[I].Durability,100);
    for(int32 I=0;I<9;++I) Pawn->TickCombatants(1.f);
    TestTrue(TEXT("Ten slashes destroy a 100 durability pillar"),FMath::IsNearlyZero(Pawn->SurfaceHeight(Pawn->CurrentCell)));

    Setup(FIntPoint(3,3),FIntPoint(7,3));
    Pawn->PlaceWall(FIntPoint(5,3)); Pawn->TickWalls(.4f);
    FIntPoint Next;
    TestTrue(TEXT("AI finds route around finite wall"),Pawn->EnemyNextStep(0,false,Next));

    Setup(FIntPoint(3,3),FIntPoint(4,3));
    Pawn->SpawnWarrior(FIntPoint(7,3));
    Pawn->ResolveFireballImpact(Pawn->Targets[0].Actor.Get(),FVector::ZeroVector);
    Pawn->ResolveFireballImpact(Pawn->Targets[0].Actor.Get(),FVector::ZeroVector);
    Pawn->TickCombatants(0.f);
    TestEqual(TEXT("No reinforcement while another enemy survives"),Pawn->Targets.Num(),2);
    Pawn->ResolveFireballImpact(Pawn->Targets[1].Actor.Get(),FVector::ZeroVector);
    Pawn->ResolveFireballImpact(Pawn->Targets[1].Actor.Get(),FVector::ZeroVector);
    Pawn->TickCombatants(0.f);
    TestEqual(TEXT("Clearing all enemies creates exactly one warrior"),Pawn->Targets.Num(),1);
    TestEqual(TEXT("Reinforcement uses enemy spawn"),Pawn->Targets[0].Cell,Pawn->EnemySpawnCell);
    TestEqual(TEXT("Reinforcement has 100 HP"),Pawn->Targets[0].Health,100);
    Pawn->TickCombatants(0.f);
    TestEqual(TEXT("Subsequent ticks do not duplicate reinforcement"),Pawn->Targets.Num(),1);

    Setup(FIntPoint(3,3),FIntPoint(7,3));
    Pawn->PlayerSpawnCell=FIntPoint(4,4);
    Pawn->SelectedSkill=2; Pawn->Cooldowns[2]=4;
    Pawn->TakeDamage(100,FDamageEvent(),nullptr,nullptr);
    Pawn->TickCombatants(1.9f);
    TestEqual(TEXT("Dead player waits for respawn"),Pawn->Health,0);
    Pawn->TickCombatants(.11f);
    TestEqual(TEXT("Respawn restores 100 HP"),Pawn->Health,100);
    TestEqual(TEXT("Respawn returns to configured cell"),Pawn->CurrentCell,Pawn->PlayerSpawnCell);
    TestEqual(TEXT("Respawn clears spell cooldown"),Pawn->Cooldowns[2],0.f);
    TestEqual(TEXT("Respawn preserves living enemies"),Pawn->Targets.Num(),1);
    TestFalse(TEXT("Respawn clears movement and jumping"),Pawn->bMoving || Pawn->bJumping || Pawn->bWaitingForMoveChord);
    Pawn->PlaceWall(Pawn->PlayerSpawnCell); Pawn->TickWalls(.4f); Pawn->UpdateElevation(.4f);
    Pawn->TakeDamage(100,FDamageEvent(),nullptr,nullptr); Pawn->TickCombatants(2.01f);
    TestTrue(TEXT("Blocked spawn falls back to safe neighbouring floor"),Pawn->CurrentCell!=Pawn->PlayerSpawnCell && Pawn->SurfaceHeight(Pawn->CurrentCell)==0.f);
    TestEqual(TEXT("Respawn does not reset the terrain or walls"),Pawn->Walls.Num(),5);
    Pawn->ResetArena();
    TestEqual(TEXT("Full reset restores initial three warriors"),Pawn->Targets.Num(),3);
    TestEqual(TEXT("Full reset clears respawn timer"),Pawn->RespawnRemaining,0.f);
    World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return true;
}
#endif
