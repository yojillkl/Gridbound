#include "GridGame.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWizardBalanceTest,"Gridbound.Combat.WizardBalance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWizardBalanceTest::RunTest(const FString& Parameters)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("World"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* P=World->SpawnActor<AGridPawn>();
    auto* PC=World->SpawnActor<APlayerController>();
    if(P && PC)
    {
        PC->Possess(P);
        auto Ready=[P,PC]()
        {
            P->bLevelMode=false; P->ResetArena();
            for(auto& T:P->Targets) if(T.Actor.IsValid()) T.Actor->Destroy();
            P->Targets.Reset(); P->RespawnPlayer(FIntPoint(2,10)); P->SpawnWarrior(FIntPoint(7,10));
            P->CombatRandom.Initialize(177);
            P->Targets[0].WisdomSave=-100;
            PC->SetControlRotation((P->Targets[0].Actor->GetActorLocation()-P->SpellOrigin()).Rotation());
        };
        Ready();
        TestEqual(TEXT("Wizard has 32 hit points"),P->Health,32);
        TestEqual(TEXT("Equal base movement"),P->MovementSpeed,GridRules::WarriorSpeed);
        for(int32 Skill:{0,2,3})
        {
            Ready(); const int32 Level=GridRules::SpellLevels[Skill],Slots=P->SpellSlots[Level-1];
            P->CastSkill(Skill);
            TestTrue(TEXT("Starting windup produces no effect or resource spend"),P->PendingCastSkill==Skill
                && P->SpellSlots[Level-1]==Slots && P->Fireballs.IsEmpty() && P->SpellVisuals.IsEmpty() && P->Targets[0].HoldRemaining==0);
            P->TickMageState(GridRules::SpellWindups[Skill]-.01f);
            P->CastSkill(Skill);
            TestEqual(TEXT("Repeated click cannot skip or restart windup"),P->SpellSlots[Level-1],Slots);
            P->TickMageState(.011f);
            TestTrue(TEXT("Completion spends once and begins action interval"),P->PendingCastSkill==INDEX_NONE
                && P->SpellSlots[Level-1]==Slots-1 && P->ActionRemaining==GridRules::SpellActionSeconds && P->SpellCastAnimation>0);
            if(Skill==0) TestEqual(TEXT("Fireball only exists after windup"),P->Fireballs.Num(),1);
            if(Skill==2) TestFalse(TEXT("Thunder only exists after windup"),P->SpellVisuals.IsEmpty());
            if(Skill==3) TestEqual(TEXT("Hold starts full duration after windup"),P->Targets[0].HoldRemaining,60.f);
            P->TickSpellCast(10.f);
            TestEqual(TEXT("Completed cast never repeats"),P->SpellSlots[Level-1],Slots-1);
        }
        Ready(); P->CastSkill(0); P->TickSpellCast(.4f); P->CancelSpellCast(); P->TickMageState(2.f);
        TestTrue(TEXT("Cancel releases no fireball and spends nothing"),P->SpellSlots[2]==2 && P->Fireballs.IsEmpty());
        Ready(); P->CastSkill(0); P->SelectSkill(2); P->TickSpellCast(1.f);
        TestTrue(TEXT("Selecting another spell cancels old windup"),P->PendingCastSkill==INDEX_NONE && P->SpellSlots[2]==2 && P->Fireballs.IsEmpty());
        Ready(); P->CastSkill(3); P->ChangeCastLevel(1);
        TestEqual(TEXT("Wheel cannot alter committed cast level"),P->PendingCastLevel,2);
        P->TickSpellCast(.8f);
        TestTrue(TEXT("Original level is charged"),P->SpellSlots[1]==2 && P->SpellSlots[2]==2);
        Ready(); P->CastSkill(3);
        auto* Wall=P->MakeBlock(FVector(600,1500,100),FVector(.2f,5,4),FLinearColor::White);
        P->TickSpellCast(.8f);
        TestTrue(TEXT("Cover acquired during windup invalidates hold"),P->SpellSlots[1]==3 && P->Targets[0].HoldRemaining==0 && P->PendingCastSkill==INDEX_NONE);
        Wall->Destroy();
        Ready(); P->CastSkill(3); P->Targets[0].Actor->SetActorLocation(FVector(2400,1500,75)); P->TickSpellCast(.8f);
        TestEqual(TEXT("Out of range at completion spends no slot"),P->SpellSlots[1],3);
        Ready(); P->CastSkill(3); P->Targets[0].Health=0; P->Targets[0].Actor->Destroy(); P->TickSpellCast(.8f);
        TestEqual(TEXT("Dead target at completion spends no slot"),P->SpellSlots[1],3);
        Ready(); P->SpawnWarrior(FIntPoint(7,12)); P->Targets[1].WisdomSave=-100;
        P->CastSkill(3); PC->SetControlRotation((P->Targets[1].Actor->GetActorLocation()-P->SpellOrigin()).Rotation()); P->TickSpellCast(.8f);
        TestTrue(TEXT("Aiming away does not switch locked hold target"),P->Targets[0].HoldRemaining>0 && P->Targets[1].HoldRemaining==0);
        Ready(); P->CastSkill(0); PC->SetControlRotation(FRotator(45,90,0)); P->TickSpellCast(.8f);
        TestTrue(TEXT("Fireball uses release aim"),P->Fireballs[0].Direction.Equals(PC->GetControlRotation().Vector(),.001f));
        Ready(); P->CastSkill(0);
        AGridPawn::FPendingAttack Attack; Attack.Roll=7; Attack.Bonus=7; Attack.Damage=8;
        P->PendingAttacks.Add(Attack); P->CastSkill(1);
        TestTrue(TEXT("Shield is immediate without interrupting windup"),P->Health==32 && P->ShieldRemaining==6
            && P->SpellSlots[0]==3 && P->PendingCastSkill==0 && P->CastWindupRemaining==.8f && P->SelectedSkill==0);
        P->TickSpellCast(.8f); TestEqual(TEXT("Offence completes after reaction"),P->Fireballs.Num(),1);
        Ready(); P->SpellSlots[0]=1; P->CastSkill(2); P->PendingAttacks.Add(Attack); P->CastSkill(1); P->TickSpellCast(.3f);
        TestTrue(TEXT("Reaction taking final shared slot cancels unaffordable cast"),P->SpellSlots[0]==0 && P->SpellVisuals.IsEmpty() && P->PendingCastSkill==INDEX_NONE);
        Ready(); P->CastSkill(3); P->TakeDamage(1,FDamageEvent(),nullptr,nullptr); P->TickSpellCast(.8f);
        TestTrue(TEXT("Nonlethal damage does not cancel preparation"),P->Targets[0].HoldRemaining>0);
        P->TakeDamage(1,FDamageEvent(),nullptr,nullptr);
        TestTrue(TEXT("Damage still does not break concentration"),P->ConcentrationRemaining>0);
        Ready(); P->CastSkill(0); P->TakeDamage(100,FDamageEvent(),nullptr,nullptr); P->TickSpellCast(1.f);
        TestTrue(TEXT("Death cancels windup without late release"),P->PendingCastSkill==INDEX_NONE && P->Fireballs.IsEmpty() && P->SpellSlots[2]==2);
        P->RespawnPlayer(FIntPoint(2,10));
        TestTrue(TEXT("Respawn restores 32 HP and clears casting"),P->Health==32 && P->PendingCastSkill==INDEX_NONE);
        Ready(); P->CastSkill(0); P->bLevelComplete=true; P->TickMageState(1.f);
        TestTrue(TEXT("Victory cancels pending cast"),P->PendingCastSkill==INDEX_NONE && P->SpellSlots[2]==2);

        Ready(); const FVector Origin(1700,1700,75);
        const FVector2D Directions[]={FVector2D(1,0),FVector2D(0,1),FVector2D(-1,0),FVector2D(-1,1)};
        for(int32 Windup=0;Windup<2;++Windup) for(auto Direction:Directions)
        {
            P->PendingCastSkill=Windup?0:INDEX_NONE;
            P->SetActorLocation(Origin); P->MoveContinuous(.1f,Direction,37.f);
            const double Expected=39.*(Direction.X<0?.75:1.)*(Windup?.3:1.);
            TestTrue(TEXT("Directional and casting speed multipliers compose without diagonal boost"),
                FMath::IsNearlyEqual(FVector::Dist2D(Origin,P->GetActorLocation()),Expected,.002));
        }
        P->CancelSpellCast(); P->SetActorLocation(Origin); P->MoveContinuous(.1f,FVector2D(1,0),0);
        TestTrue(TEXT("Cancel restores normal speed"),FMath::IsNearlyEqual(P->GetActorLocation().X-Origin.X,39.,.002));
    }
    else AddError(TEXT("Could not create actors"));
    World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return !HasAnyErrors();
}
#endif
