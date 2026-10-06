#include "GridGame.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCombatReadoutTest,"Gridbound.Combat.Readouts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCombatReadoutTest::RunTest(const FString& Parameters)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("World"),World)) return false;
    GEngine->CreateNewWorldContext(World->WorldType).SetCurrentWorld(World);
    auto* P=World->SpawnActor<AGridPawn>();
    if(TestNotNull(TEXT("Pawn"),P))
    {
        auto Ready=[P]()
        {
            P->bLevelMode=false; P->ResetArena();
            for(auto& T:P->Targets) if(T.Actor.IsValid()) T.Actor->Destroy();
            P->Targets.Reset(); P->RespawnPlayer(FIntPoint(2,10)); P->SpawnWarrior(FIntPoint(7,10));
            P->CombatRandom.Initialize(177);
        };
        Ready();
        auto Random=P->CombatRandom;
        for(int32 I=0;I<5;++I) P->AddCombatReadout(true,TEXT("Player"),TEXT("8d6"),TEXT("Damage"),I,6);
        for(int32 I=0;I<5;++I) P->AddCombatReadout(false,TEXT("Enemy"),TEXT("d20"),TEXT("Hit"),I,20);
        TestEqual(TEXT("Three bounded entries per side"),P->CombatReadouts.Num(),6);
        TestEqual(TEXT("Oldest overflow entry removed"),P->CombatReadouts[0].Value,2);
        TestEqual(TEXT("Feedback consumes no combat RNG"),P->RollDice(1,20),Random.RandRange(1,20));
        P->TickCombatReadouts(.4f);
        TestTrue(TEXT("Result survives animation interval"),P->CombatReadouts.Num()==6 && P->CombatReadouts[0].Age>.32f);
        P->TickCombatReadouts(3.f);
        TestTrue(TEXT("Entries expire independently of combat"),P->CombatReadouts.IsEmpty());

        Ready(); P->CastSkill(1);
        TestTrue(TEXT("Shield fixed value is on player side"),P->CombatReadouts.Num()==1 && P->CombatReadouts[0].bPlayer && P->CombatReadouts[0].Sides==0 && P->CombatReadouts[0].Value==5);
        P->CastSkill(1);
        TestEqual(TEXT("Rejected casts do not create success feedback"),P->CombatReadouts.Num(),1);
        P->ResetWizardState(); TestTrue(TEXT("Restart clears old results"),P->CombatReadouts.IsEmpty());

        Ready(); P->Targets[0].DexteritySave=100;
        Random=P->CombatRandom; int32 Damage=0;
        for(int32 I=0;I<8;++I) Damage+=Random.RandRange(1,6);
        P->ExplodeFireball(P->Targets[0].Actor->GetActorLocation());
        TestEqual(TEXT("Fireball shows the exact damage roll"),P->CombatReadouts.Last().Value,Damage);
        TestTrue(TEXT("Fireball readout includes applied half damage"),P->CombatReadouts.Last().Result.Contains(FString::Printf(TEXT("对敌 %d 伤害"),Damage/2)));
        TestEqual(TEXT("Feedback does not alter fireball damage"),P->Targets[0].Health,GridRules::WarriorHealth-Damage/2);
        TestTrue(TEXT("Fireball displays actual save and half damage"),P->CombatReadouts.Last().Details.Contains(TEXT("敏捷")) && P->CombatReadouts.Last().Details.Contains(TEXT("半伤")));

        Ready(); P->Targets[0].Actor->SetActorLocation(FVector(600,1500,75));
        P->CastThunderwave(FVector::ForwardVector,1);
        TestTrue(TEXT("Thunder save separates Constitution and proficiency"),P->CombatReadouts.Last().Details.Contains(TEXT("体质+3")) && P->CombatReadouts.Last().Details.Contains(TEXT("熟练+3")));
        Ready(); P->Targets[0].HoldRemaining=60.f;
        P->ExplodeFireball(P->Targets[0].Actor->GetActorLocation());
        TestTrue(TEXT("Paralyzed target reports automatic failure without fake save"),P->CombatReadouts.Last().Details.Contains(TEXT("自动失败")) && !P->CombatReadouts.Last().Details.Contains(TEXT("d20")));

        Ready(); P->Targets[0].WisdomSave=100; Random=P->CombatRandom;
        const int32 Save=Random.RandRange(1,20)+100;
        P->CastHoldPerson({0});
        TestTrue(TEXT("Hold shows actual target save, not a fabricated DC roll"),P->CombatReadouts.Last().bPlayer && P->CombatReadouts.Last().Value==Save && P->CombatReadouts.Last().Sides==20);

        Ready(); P->ShieldRemaining=6.f;
        AGridPawn::FPendingAttack Attack; Attack.Roll=7; Attack.Bonus=8; Attack.Damage=20;
        P->ResolvePendingAttack(Attack);
        TestTrue(TEXT("Blocked attack is enemy-side with zero damage"),!P->CombatReadouts.Last().bPlayer && P->CombatReadouts.Last().Value==15 && P->CombatReadouts.Last().Result.Contains(TEXT("0 伤害")) && P->Health==GridRules::MaxHealth);
        Attack.Roll=20; P->ResolvePendingAttack(Attack);
        TestTrue(TEXT("Critical attack reports real damage"),P->CombatReadouts.Last().Result.Contains(TEXT("20 伤害")) && P->CombatReadouts.Last().Result.Contains(TEXT("暴击")));

        Ready(); P->Targets[0].Health=GridRules::WarriorHealth-1; Random=P->CombatRandom;
        const int32 Healing=Random.RandRange(1,10)+5;
        P->UseWarriorSecondWind(0);
        TestTrue(TEXT("Healing distinguishes rolled amount from capped recovery"),!P->CombatReadouts.Last().bPlayer && P->CombatReadouts.Last().Value==Healing && P->CombatReadouts.Last().Result.Contains(TEXT("恢复 1 生命")));

        Ready(); P->Targets[0].StrikeCell=P->CurrentCell;
        P->ResolveWarriorSkill(0);
        TestTrue(TEXT("Spatial miss does not invent a die roll"),!P->CombatReadouts.IsEmpty() && P->CombatReadouts.Last().Sides==0 && P->CombatReadouts.Last().Value==0);
    }
    World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return !HasAnyErrors();
}
#endif
