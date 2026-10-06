#include "GridGame.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWarriorPerceptionTest,"Gridbound.Combat.WarriorPerception",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWarriorPerceptionTest::RunTest(const FString& Parameters)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("World"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* P=World->SpawnActor<AGridPawn>();
    if(TestNotNull(TEXT("Player"),P))
    {
        auto Ready=[P]()
        {
            for(auto& T:P->Targets) if(T.Actor.IsValid()) T.Actor->Destroy();
            P->Targets.Reset(); P->bLevelMode=false; P->bEncounterStarted=false;
            P->RespawnPlayer(FIntPoint(12,10)); P->SpawnWarrior(FIntPoint(4,10));
            P->Targets[0].ChargeCooldown=100.f;
        };
        Ready();
        auto& T=P->Targets[0];
        const FVector Origin=T.Actor->GetActorLocation();
        P->SetActorLocation(Origin+FVector(GridRules::EnemySightRange,0,0));
        TestTrue(TEXT("Acquire at 18 cells"),P->EnemySeesPlayer(T));
        P->SetActorLocation(Origin+FVector(GridRules::EnemySightRange+1,0,0));
        TestFalse(TEXT("Idle sight respects boundary"),P->EnemySeesPlayer(T));
        P->RememberWarriorPlayer(T,P->GetActorLocation());
        P->SetActorLocation(Origin+FVector(GridRules::EnemyTrackingRange,0,0));
        TestTrue(TEXT("Track at 24 cells while alerted"),P->EnemySeesPlayer(T));
        P->SetActorLocation(Origin+FVector(GridRules::EnemyTrackingRange+1,0,0));
        TestFalse(TEXT("Tracking respects outer boundary"),P->EnemySeesPlayer(T));
        T.AlertTime=0.f;
        P->SetActorLocation(Origin+FVector(20*GridRules::CellSize,0,0));
        TestFalse(TEXT("Expired alert restores acquisition radius"),P->EnemySeesPlayer(T));

        P->SetActorLocation(Origin+FVector(900,0,0));
        AActor* Partial=P->MakeBlock(Origin+FVector(450,0,50),FVector(.2f,3,.2f),FLinearColor::White);
        TestTrue(TEXT("Visible torso survives blocked head ray"),P->EnemySeesPlayer(T));
        Partial->Destroy();
        AActor* Cover=P->MakeBlock(Origin+FVector(450,0,100),FVector(.2f,12,5),FLinearColor::White);
        TestFalse(TEXT("Full cover blocks all sight samples"),P->EnemySeesPlayer(T));
        P->RememberWarriorPlayer(T,P->GetActorLocation());
        const FVector Memory=T.LastKnownPosition;
        P->SetActorLocation(P->GetActorLocation()+FVector(0,150,0));
        P->TickWarriorBrain(0,.11f);
        TestTrue(TEXT("Hidden movement does not update memory"),T.LastKnownPosition.Equals(Memory));
        TestFalse(TEXT("Hidden player cannot trigger charge"),P->BeginWarriorCharge(0));
        Cover->Destroy();

        Ready();
        P->bLevelMode=true; P->RespawnPlayer(FIntPoint(2,10));
        TestFalse(TEXT("Preparation area protected before combat"),P->EnemySeesPlayer(P->Targets[0]));
        P->ResolveFireballImpact(P->Targets[0].Actor.Get(),P->Targets[0].Actor->GetActorLocation());
        P->Targets[0].AlertTime=0.f;
        TestTrue(TEXT("Attack permanently ends preparation protection"),P->EnemySeesPlayer(P->Targets[0]));
        P->StartEncounter();
        TestFalse(TEXT("New encounter restores preparation protection"),P->EnemySeesPlayer(P->Targets[0]));
        P->RespawnPlayer(FIntPoint(4,10)); P->TickLevelCombatants(.01f);
        P->RespawnPlayer(FIntPoint(2,10)); P->Targets[0].AlertTime=0.f;
        TestTrue(TEXT("Entering arena permanently ends preparation protection"),P->EnemySeesPlayer(P->Targets[0]));

        Ready();
        auto& Search=P->Targets[0];
        P->RememberWarriorPlayer(Search,Search.Actor->GetActorLocation());
        Search.LastSeenDirection=FVector(0,1,0);
        P->SetActorLocation(FVector(10000,10000,75));
        P->TickWarriorBrain(0,.13f);
        TestTrue(TEXT("Arrival begins area search"),Search.bSearchingArea);
        TestTrue(TEXT("First search follows last observed direction"),Search.SearchGoal.Y>Search.LastKnownPosition.Y);
        const FVector SearchGoal=Search.SearchGoal;
        for(int32 I=0;I<3;++I) P->TickWarriorBrain(0,.13f);
        TestTrue(TEXT("Search goal persists between decisions"),Search.SearchGoal.Equals(SearchGoal));
        TestTrue(TEXT("Search advances without returning to memory point"),Search.Actor->GetActorLocation().Y>Search.LastKnownPosition.Y+100.f);
        for(int32 I=0;I<65;++I) P->TickWarriorBrain(0,.1f);
        TestTrue(TEXT("Local search expires after six seconds"),Search.AlertTime<=0.f && !Search.bSearchingArea);

        Ready();
        auto& Pursuit=P->Targets[0];
        P->RememberWarriorPlayer(Pursuit,FVector(4200,1500,75));
        P->SetActorLocation(FVector(10000,10000,75));
        Pursuit.StaggerRemaining=14.f; P->TickWarriorBrain(0,14.f);
        TestTrue(TEXT("Memory lasts beyond former eight seconds"),Pursuit.AlertTime>GridRules::EnemySearchSeconds);
        P->TickWarriorBrain(0,1.1f);
        TestTrue(TEXT("Fifteen second pursuit transitions to local search"),Pursuit.bSearchingArea);
        for(int32 I=0;I<65;++I) P->TickWarriorBrain(0,.1f);
        TestTrue(TEXT("Lost target has bounded total alert"),Pursuit.AlertTime<=0.f);

        Ready();
        AGridPawn::FWall Wall; Wall.Cell=FIntPoint(10,10); Wall.Age=1.f;
        Wall.Actor=P->MakeBlock(GridRules::Center(Wall.Cell,225),FVector(1.5f,1.5f,4.5f),FLinearColor::White);
        P->Walls.Add(Wall);
        FIntPoint Next,Reached;
        TestTrue(TEXT("Unreachable goal yields reachable approach"),P->EnemyNextStep(0,false,Next,&Wall.Cell,&Reached));
        TestTrue(TEXT("Approach ends beside blocked goal"),Reached!=Wall.Cell && GridRules::Distance(Reached,Wall.Cell)==1);
        TestFalse(TEXT("Exact search rejects unreachable destination"),P->EnemyNextStep(0,false,Next,&Wall.Cell,nullptr,false));
        Wall.Actor->Destroy(); P->Walls.Reset();

        Ready();
        auto& Stuck=P->Targets[0];
        const FVector Before=Stuck.Actor->GetActorLocation();
        AActor* Block=P->MakeBlock(Before+FVector(45,0,75),FVector(.2f,3,4),FLinearColor::White);
        Stuck.StuckTime=1.f; P->TickWarriorBrain(0,.13f);
        TestTrue(TEXT("Stuck recovery chooses a traversable detour"),Stuck.TacticRemaining>0.f);
        TestTrue(TEXT("Stuck recovery makes progress"),FVector::Dist2D(Before,Stuck.Actor->GetActorLocation())>20.f);
        Block->Destroy();
    }
    World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return !HasAnyErrors();
}
#endif
