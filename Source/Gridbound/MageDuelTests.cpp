#include "GridGame.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMageDuelTest,"Gridbound.Combat.MageDuel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMageDuelTest::RunTest(const FString& Parameters)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("World"),World)) return false;
    GEngine->CreateNewWorldContext(World->WorldType).SetCurrentWorld(World);
    auto* P=World->SpawnActor<AGridPawn>();
    auto* PC=World->SpawnActor<APlayerController>();
    if(P && PC)
    {
        PC->Possess(P);
        auto Ready=[P,PC](FIntPoint Enemy=FIntPoint(7,10))
        {
            P->bLevelMode=false; P->ResetArena();
            for(auto& T:P->Targets) if(T.Actor.IsValid()) T.Actor->Destroy();
            P->Targets.Reset(); P->RespawnPlayer(FIntPoint(2,10)); P->SpawnWarrior(Enemy);
            P->CombatRandom.Initialize(177);
            PC->SetControlRotation((P->Targets[0].Actor->GetActorLocation()-P->SpellOrigin()).Rotation());
        };
        auto Expected=[P](int32 Count,int32 Sides)
        {
            auto Copy=P->CombatRandom;
            int32 Total=0; for(int32 I=0;I<Count;++I) Total+=Copy.RandRange(1,Sides);
            return Total;
        };
        auto Cast=[P](int32 Skill) { P->CastSkill(Skill); if(Skill!=1) P->TickSpellCast(GridRules::SpellWindups[Skill]); };
        Ready();
        TestTrue(TEXT("Level five slot allocation"),P->SpellSlots[0]==4 && P->SpellSlots[1]==3 && P->SpellSlots[2]==2);
        P->SelectSkill(0);
        TestTrue(TEXT("Selection does not cast or spend"),P->Fireballs.IsEmpty() && P->SpellSlots[2]==2);
        Cast(0);
        TestTrue(TEXT("Fireball spends exactly one third slot"),P->Fireballs.Num()==1 && P->SpellSlots[2]==1 && P->SpellSlots[0]==4);
        Cast(0);
        TestTrue(TEXT("Action gating does not spend twice"),P->Fireballs.Num()==1 && P->SpellSlots[2]==1);
        P->TickMageState(1.01f); Cast(0);
        for(int32 I=0;I<12;++I) { P->TickMageState(1.01f); Cast(0); }
        TestTrue(TEXT("Only two fireballs, no fallback or underflow"),P->Fireballs.Num()==2 && P->SpellSlots[2]==0 && P->SelectedSkill==0);
        TestEqual(TEXT("Lower slots cannot fund fireball"),P->SpellSlots[0],4);
        P->ChangeCastLevel(-1);
        TestEqual(TEXT("Fireball minimum is third level"),P->CastLevels[0],3);

        Ready(); Cast(3);
        TestEqual(TEXT("Hold spends a second slot"),P->SpellSlots[1],2);
        P->TickMageState(1.01f); P->SelectedSkill=3; P->ChangeCastLevel(1); Cast(3);
        TestEqual(TEXT("Upcast hold spends shared third slot"),P->SpellSlots[2],1);
        P->TickMageState(1.01f); Cast(0);
        TestEqual(TEXT("Fireball competes with upcast hold"),P->SpellSlots[2],0);
        Ready(); P->SpellSlots[0]=0; Cast(2);
        TestEqual(TEXT("No automatic upgrade"),P->SpellSlots[1],3);
        P->SelectedSkill=2; P->ChangeCastLevel(1); Cast(2);
        TestEqual(TEXT("Explicit upgrade spends second slot"),P->SpellSlots[1],2);
        P->TickMageState(50.f);
        TestEqual(TEXT("Waiting does not regenerate slots"),P->SpellSlots[0],0);
        TestTrue(TEXT("No wall or rain remains in loadout"),P->Walls.IsEmpty() && P->RainEffects.IsEmpty());

        Ready(); P->Targets[0].WisdomSave=-100;
        P->Targets[0].Windup=.8f; P->Targets[0].bCharging=true; P->Targets[0].StrikesRemaining=3;
        Cast(3);
        TestTrue(TEXT("Failed wisdom save causes paralysis and concentration"),P->Targets[0].HoldRemaining==60.f && P->ConcentrationRemaining==60.f);
        TestTrue(TEXT("Paralysis cancels attack and charge"),P->Targets[0].Windup==0 && !P->Targets[0].bCharging && P->Targets[0].StrikesRemaining==0);
        const FVector HeldPosition=P->Targets[0].Actor->GetActorLocation();
        P->TickWarriorBrain(0,1.f);
        TestTrue(TEXT("Held enemy cannot move"),HeldPosition.Equals(P->Targets[0].Actor->GetActorLocation()));
        P->ResolveWarriorSkill(0);
        TestEqual(TEXT("Held enemy cannot strike"),P->Health,GridRules::MaxHealth);
        P->TakeDamage(1,FDamageEvent(),nullptr,nullptr); P->TakeDamage(1,FDamageEvent(),nullptr,nullptr);
        TestTrue(TEXT("Requested no-damage-interruption rule"),P->ConcentrationRemaining>0.f);
        P->TickMageState(5.f);
        TestTrue(TEXT("No early repeated saving throw"),P->Targets[0].HoldRemaining>0.f);
        P->Targets[0].WisdomSave=100; P->TickMageState(1.f);
        TestTrue(TEXT("Round-end save releases all concentration state"),P->Targets[0].HoldRemaining==0.f && P->ConcentrationRemaining==0.f && !P->Targets[0].HoldVisual.IsValid());

        Ready(); P->Targets[0].WisdomSave=100; Cast(3);
        TestTrue(TEXT("Resisted hold still costs slot"),P->Targets[0].HoldRemaining==0 && P->SpellSlots[1]==2);
        Ready(); P->Targets[0].bHumanoid=false; Cast(3);
        TestEqual(TEXT("Wrong creature type does not spend"),P->SpellSlots[1],3);
        Ready(); PC->SetControlRotation(FRotator(80,0,0)); Cast(3);
        TestEqual(TEXT("Missing target does not spend"),P->SpellSlots[1],3);
        Ready(); auto* Barrier=P->MakeBlock(FVector(600,1500,100),FVector(.2f,5,4),FLinearColor::White);
        Cast(3); TestEqual(TEXT("Hold cannot target through cover"),P->SpellSlots[1],3); Barrier->Destroy();
        Ready(); P->Targets[0].Actor->SetActorLocation(FVector(2300,1500,75));
        PC->SetControlRotation((P->Targets[0].Actor->GetActorLocation()-P->SpellOrigin()).Rotation());
        Cast(3); TestEqual(TEXT("Hold range is enforced"),P->SpellSlots[1],3);
        Ready(); P->SpawnWarrior(FIntPoint(7,11));
        for(auto& T:P->Targets) T.WisdomSave=-100;
        P->CastLevels[3]=3; Cast(3);
        TestTrue(TEXT("Third-level hold acquires two previewed targets"),P->AimTrace.Enemies.Num()==2 && P->Targets[0].HoldRemaining>0 && P->Targets[1].HoldRemaining>0);
        P->TickMageState(1.01f); Cast(0);
        TestTrue(TEXT("Non-concentration spell preserves hold"),P->Targets[0].HoldRemaining>0);
        P->EndConcentration();
        TestTrue(TEXT("Manual release clears every target"),P->Targets[0].HoldRemaining==0 && P->Targets[1].HoldRemaining==0);
        P->ActionRemaining=0; Cast(3); P->TickMageState(61.f);
        TestEqual(TEXT("Hold expires even after large frame"),P->ConcentrationRemaining,0.f);

        Ready(FIntPoint(4,10)); P->Targets[0].ConstitutionSave=-100; P->Targets[0].WetRemaining=3.f;
        const int32 ThunderDamage=Expected(2,8);
        Cast(2);
        TestEqual(TEXT("Thunder uses 2d8 without wet multiplier"),P->Targets[0].Health,GridRules::WarriorHealth-ThunderDamage);
        TestTrue(TEXT("Failed thunder save pushes exactly three metres"),P->Targets[0].Actor->GetActorLocation().Equals(FVector(900,1500,75)));
        TestEqual(TEXT("Thunder uses one first slot"),P->SpellSlots[0],3);
        Ready(FIntPoint(4,10)); P->Targets[0].ConstitutionSave=100; P->Targets[0].HoldRemaining=60;
        P->CastLevels[2]=3;
        const int32 UpcastDamage=Expected(4,8); const FVector Before=P->Targets[0].Actor->GetActorLocation();
        Cast(2);
        TestEqual(TEXT("Paralysis does not auto-fail constitution"),P->Targets[0].Health,GridRules::WarriorHealth-UpcastDamage/2);
        TestTrue(TEXT("Successful thunder save prevents push"),Before.Equals(P->Targets[0].Actor->GetActorLocation()));
        Ready(FIntPoint(4,10)); P->Targets[0].ConstitutionSave=-100;
        Barrier=P->MakeBlock(FVector(660,1500,100),FVector(.2f,5,4),FLinearColor::White);
        Cast(2);
        TestTrue(TEXT("Push cannot tunnel through a wall"),P->Targets[0].Actor->GetActorLocation().X<650.f);
        Barrier->Destroy();
        Ready(FIntPoint(1,10)); PC->SetControlRotation(FRotator::ZeroRotator); Cast(2);
        TestEqual(TEXT("Thunder does not hit behind caster"),P->Targets[0].Health,GridRules::WarriorHealth);

        Ready(); P->SpawnWarrior(FIntPoint(8,10));
        P->Targets[0].DexteritySave=-100; P->Targets[1].DexteritySave=100;
        P->Targets[0].Windup=.8f;
        const int32 BlastDamage=Expected(8,6);
        P->ExplodeFireball(P->Targets[0].Actor->GetActorLocation());
        TestEqual(TEXT("Fireball failure takes full 8d6"),P->Targets[0].Health,GridRules::WarriorHealth-BlastDamage);
        TestEqual(TEXT("Nearby save takes half of same damage roll"),P->Targets[1].Health,GridRules::WarriorHealth-BlastDamage/2);
        TestEqual(TEXT("Fireball does not add an interruption"),P->Targets[0].Windup,.8f);
        TestEqual(TEXT("Reduced fireball radius is three metres"),GridRules::FireballBlastRadius,300.f);
        Ready(); P->SpawnWarrior(FIntPoint(8,10));
        const FVector BoundaryCenter=P->Targets[0].Actor->GetActorLocation();
        P->Targets[0].Actor->SetActorLocation(BoundaryCenter+FVector(0,299,0));
        P->Targets[1].Actor->SetActorLocation(BoundaryCenter+FVector(0,-301,0));
        P->Targets[0].DexteritySave=-100;
        P->ExplodeFireball(BoundaryCenter);
        TestTrue(TEXT("Target just inside three metres is damaged"),P->Targets[0].Health<GridRules::WarriorHealth);
        TestEqual(TEXT("Target just outside three metres is safe"),P->Targets[1].Health,GridRules::WarriorHealth);
        Ready(); P->Targets[0].DexteritySave=100; P->Targets[0].HoldRemaining=60;
        const int32 HeldDamage=Expected(8,6); P->ExplodeFireball(P->Targets[0].Actor->GetActorLocation());
        TestEqual(TEXT("Paralyzed target auto-fails dexterity"),P->Targets[0].Health,GridRules::WarriorHealth-HeldDamage);
        TestTrue(TEXT("Damage does not break target paralysis"),P->Targets[0].HoldRemaining>0.f);
        Ready(); const FVector Blast=P->Targets[0].Actor->GetActorLocation()-FVector(200,0,0);
        Barrier=P->MakeBlock(Blast+FVector(100,0,0),FVector(.2f,5,4),FLinearColor::White);
        P->ExplodeFireball(Blast); TestEqual(TEXT("Solid cover blocks blast"),P->Targets[0].Health,GridRules::WarriorHealth); Barrier->Destroy();
        Ready(); P->ExplodeFireball(P->GetActorLocation());
        TestTrue(TEXT("Fireball includes caster in blast"),P->Health<GridRules::MaxHealth);
        Ready(); PC->SetControlRotation(FRotator(45,0,0)); Cast(0); P->TickFireballs(10.f);
        TestTrue(TEXT("Fireball detonates at range without actor hit"),P->Fireballs.IsEmpty() && !P->SpellVisuals.IsEmpty());

        Ready();
        P->Targets[0].Actor->Destroy(); P->Targets.Reset();
        PC->SetControlRotation(FRotator(80,0,0)); P->SelectSkill(1);
        P->ComputeSkillAim(FVector::ZeroVector);
        TestTrue(TEXT("Shield preview targets self without direction or enemy"),P->bHasAim && P->bValidAim && P->AimPoint.Equals(P->SpellOrigin()));
        Cast(1);
        TestTrue(TEXT("Proactive shield spends one slot and protects self"),P->SpellSlots[0]==3 && P->ShieldRemaining==6 && P->ShieldVisual.IsValid());
        Cast(1); TestEqual(TEXT("Active proactive shield cannot spend twice"),P->SpellSlots[0],3);
        P->TickMageState(6.f); Cast(1);
        TestTrue(TEXT("Shield can be renewed after reaction recovery"),P->SpellSlots[0]==2 && P->ShieldRemaining==6);
        Ready(); P->SpellSlots[0]=0; Cast(1);
        TestTrue(TEXT("Proactive shield still requires selected slot"),P->ShieldRemaining==0 && P->SpellSlots[1]==3);
        Ready();
        AGridPawn::FPendingAttack Attack; Attack.Roll=7; Attack.Bonus=8; Attack.Damage=20;
        Attack.bTrip=true; Attack.Source=P->Targets[0].Actor;
        P->PendingAttacks.Add(Attack); P->SelectedSkill=0; P->ActionRemaining=1.f; Cast(1);
        TestTrue(TEXT("Shield blocks same attack roll even during action"),P->Health==GridRules::MaxHealth && P->TripRemaining==0 && P->SpellSlots[0]==3 && P->ShieldRemaining==6);
        TestEqual(TEXT("Reaction preserves selected offence"),P->SelectedSkill,0);
        Cast(1); TestEqual(TEXT("No repeated shield consumption"),P->SpellSlots[0],3);
        Attack.Roll=20; Attack.Damage=20; Attack.bTrip=false; P->ResolvePendingAttack(Attack);
        TestEqual(TEXT("Critical attack bypasses shield AC"),P->Health,GridRules::MaxHealth-20);
        P->TickMageState(6.f);
        TestTrue(TEXT("Shield and reaction expire together"),P->ShieldRemaining==0 && P->ReactionRemaining==0);
        Ready(); Attack.Roll=7; Attack.Damage=20; Attack.bTrip=true; Attack.SaveDC=100; Attack.Source=P->Targets[0].Actor; P->PendingAttacks.Add(Attack);
        P->TickMageState(.66f);
        TestTrue(TEXT("Declined reaction applies damage and trip"),P->Health<GridRules::MaxHealth-20 && P->TripRemaining>0);
        Ready(); P->SpellSlots[0]=0; P->PendingAttacks.Add(Attack); Cast(1);
        TestTrue(TEXT("Unaffordable reaction stays pending without spending higher slots"),P->PendingAttacks.Num()==1 && P->SpellSlots[1]==3);
        P->CastLevels[1]=2; Cast(1);
        TestTrue(TEXT("Explicit second-level shield shares hold resource"),P->Health==GridRules::MaxHealth && P->SpellSlots[1]==2);
        Ready(); P->Targets[0].WisdomSave=-100; Cast(3); P->TakeDamage(100,FDamageEvent(),nullptr,nullptr);
        TestTrue(TEXT("Death ends hold and pending damage"),P->ConcentrationRemaining==0 && P->Targets[0].HoldRemaining==0 && P->PendingAttacks.IsEmpty());
        P->bLevelMode=true; P->TickLevelCombatants(2.1f);
        TestTrue(TEXT("Retry restores slots and unchanged enemy"),P->Health==GridRules::MaxHealth && P->SpellSlots[0]==4 && P->SpellSlots[1]==3 && P->SpellSlots[2]==2 && P->Targets[0].Health==GridRules::WarriorHealth);
        P->bLevelComplete=true; Cast(0);
        TestEqual(TEXT("Victory prevents resource spend"),P->SpellSlots[2],2);
    }
    else AddError(TEXT("Could not create player/controller"));
    World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return !HasAnyErrors();
}
#endif
