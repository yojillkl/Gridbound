#include "GridGame.h"
#include "GridArt.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLightningSpellTest,"Gridbound.Combat.LightningAndAim",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLightningSpellTest::RunTest(const FString& Parameters)
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
            P->Targets.Reset(); P->RespawnPlayer(FIntPoint(2,10)); P->SpawnWarrior(FIntPoint(5,10));
            // Legacy environment fixture survives repeated damage independently of enemy balance.
            P->Targets[0].Health=P->Targets[0].MaxHealth=350;
            PC->SetControlRotation((P->Targets[0].Actor->GetActorLocation()-P->SpellOrigin()).Rotation());
        };
        auto Direction=[P](){return (P->Targets[0].Actor->GetActorLocation()-P->SpellOrigin()).GetSafeNormal();};

        Ready(); P->CastLightning(Direction());
        TestEqual(TEXT("Dry lightning matches fireball damage"),P->Targets[0].Health,350-GridRules::FireballDamage);

        Ready(); TestTrue(TEXT("Rain casts on enemy"),P->CastRain(FIntPoint(5,10)));
        TestEqual(TEXT("Rain applies wet residue"),P->Targets[0].WetRemaining,GridRules::WetLifetime);
        const auto PredictedCells=P->LightningGroundPlan(P->TraceSpell(3,Direction()));
        P->CastLightning(Direction());
        TestEqual(TEXT("Wet target takes double lightning damage"),P->Targets[0].Health,250);
        TestEqual(TEXT("Conductive preview equals actual ground area"),P->ElectricCells.Num(),PredictedCells.Num());
        for(auto C:PredictedCells) TestTrue(TEXT("Preview cell really electrified"),P->ElectricCells.ContainsByPredicate([C](const AGridPawn::FElectricCell& E){return E.Cell==C;}));
        P->TickElectricity(1.f);
        TestEqual(TEXT("Ground damage is ten per second without wet multiplier"),P->Targets[0].Health,240);

        Ready(); P->CastRain(FIntPoint(5,10)); P->TickWetTerrain(6.f);
        P->CastRain(FIntPoint(5,10));
        TestEqual(TEXT("Rain refreshes wet duration"),P->Targets[0].WetRemaining,GridRules::WetLifetime);
        P->TickWetTerrain(11.1f); P->CastLightning(Direction());
        TestEqual(TEXT("Wet expiration removes damage multiplier"),P->Targets[0].Health,300);

        Ready(); P->Targets[0].Actor->SetActorLocation(GridRules::Center(FIntPoint(15,10),75));
        P->CastRain(FIntPoint(5,10));
        TestEqual(TEXT("Rain does not wet distant enemy"),P->Targets[0].WetRemaining,0.f);
        P->Targets[0].Actor->SetActorLocation(GridRules::Center(FIntPoint(5,10),75)); P->TickWetTerrain(1.f);
        TestEqual(TEXT("Entering active rain gets wet"),P->Targets[0].WetRemaining,GridRules::WetLifetime);
        P->TickWetTerrain(1.f);
        TestEqual(TEXT("Concentrated rain sustains wet"),P->Targets[0].WetRemaining,GridRules::WetLifetime);
        P->Targets[0].Actor->SetActorLocation(GridRules::Center(FIntPoint(15,10),75)); P->TickWetTerrain(1.f);
        TestEqual(TEXT("Leaving rain starts wet residue countdown"),P->Targets[0].WetRemaining,GridRules::WetLifetime-1.f);

        Ready(); P->MakeMud(FIntPoint(5,10));
        const auto Patch=P->ConductiveArea(FIntPoint(5,10));
        P->ElectrifyCells(Patch); P->ElectrifyCells(Patch);
        TestEqual(TEXT("Repeated electrification does not stack cells"),P->ElectricCells.Num(),Patch.Num());
        P->TickElectricity(20.f);
        TestEqual(TEXT("Large frame clips damage at ten seconds"),P->Targets[0].Health,250);
        TestEqual(TEXT("Electric area expires"),P->ElectricCells.Num(),0);
        P->TickElectricity(2.f);
        TestEqual(TEXT("No damage after electricity expires"),P->Targets[0].Health,250);

        Ready(); P->MakeMud(FIntPoint(5,10)); P->ElectrifyCells(P->ConductiveArea(FIntPoint(5,10)));
        P->Targets[0].FootHeight=150; P->TickElectricity(1.f);
        TestEqual(TEXT("Raised enemy avoids ground electricity"),P->Targets[0].Health,350);
        P->Targets[0].FootHeight=0; P->TickElectricity(.05f);
        P->Targets[0].Actor->SetActorLocation(GridRules::Center(FIntPoint(6,10),75)); P->TickElectricity(.05f);
        P->Targets[0].Actor->SetActorLocation(GridRules::Center(FIntPoint(5,10),75)); P->TickElectricity(.05f);
        TestEqual(TEXT("Leaving electric ground clears fractional damage"),P->Targets[0].Health,350);

        Ready(); P->MakeMud(FIntPoint(5,10)); P->MakeMud(FIntPoint(7,10));
        TestFalse(TEXT("Dry gap stops conduction"),P->ConductiveArea(FIntPoint(5,10)).Contains(FIntPoint(7,10)));
        TestTrue(TEXT("Dry ground cannot be electrified"),P->ConductiveArea(FIntPoint(6,10)).IsEmpty());
        P->MuddyCells[0].Remaining=.1f; P->ElectrifyCells(P->ConductiveArea(FIntPoint(5,10)));
        P->TickElectricity(1.f);
        TestEqual(TEXT("Damage clips at drying time"),P->Targets[0].Health,349);

        Ready(); const FIntPoint River(10,6);
        TestTrue(TEXT("Water conducts without rain"),P->IsConductiveCell(River));
        const auto WaterCells=P->ConductiveArea(River);
        TestTrue(TEXT("Connected water conducts locally"),WaterCells.Num()>1);
        for(auto C:WaterCells) TestTrue(TEXT("Conduction has bounded radius"),(C-River).SizeSquared()<=GridRules::ConductRadius*GridRules::ConductRadius);
        P->Targets[0].Actor->SetActorLocation(GridRules::Center(River,75));
        P->ElectrifyCells(WaterCells); P->TickElectricity(1.f);
        TestEqual(TEXT("Enemy on electrified water takes damage"),P->Targets[0].Health,340);

        Ready(); P->SpawnWarrior(FIntPoint(7,10));
        P->Targets[1].Health=P->Targets[1].MaxHealth=350;
        P->CastLightning(Direction());
        TestEqual(TEXT("Beam hits first target"),P->Targets[0].Health,300);
        TestEqual(TEXT("Beam pierces to second target"),P->Targets[1].Health,300);

        Ready();
        AActor* Barrier=P->MakeBlock(FVector(600,1500,150),FVector(.2f,3,4),FLinearColor::White);
        const auto BlockedTrace=P->TraceSpell(3,Direction());
        TestTrue(TEXT("Blocked beam trace has no enemy"),BlockedTrace.Enemies.IsEmpty());
        TestTrue(TEXT("Beam trace ends at wall"),BlockedTrace.End.X<600);
        P->CastLightning(Direction());
        TestEqual(TEXT("Beam cannot damage through wall"),P->Targets[0].Health,350);
        Barrier->Destroy();

        Ready(); P->Targets[0].Actor->SetActorLocation(GridRules::Center(FIntPoint(15,10),75));
        const auto FarTrace=P->TraceSpell(3,Direction()); P->CastLightning(Direction());
        TestEqual(TEXT("Beam cannot reach beyond range"),P->Targets[0].Health,350);
        TestTrue(TEXT("Beam trace ends at range limit"),FMath::IsNearlyEqual(float(FVector::Dist(P->SpellOrigin(),FarTrace.End)),GridRules::LightningRange,.01f));

        Ready(); P->SelectedSkill=0; P->ComputeSkillAim(Direction());
        TestEqual(TEXT("Fireball preview acquires enemy"),P->AimEnemy,0);
        P->LaunchFireball(P->AimDirection); P->TickFireballs(1.f);
        TestTrue(TEXT("Fireball agrees with preview"),P->Targets[0].Health<350);

        Ready(); P->MakeMud(FIntPoint(5,10));
        AActor* Floor=GridArt::CreateTile(World,FIntPoint(5,10),P->BaseMaterial);
        const FVector GroundDirection=(GridRules::Center(FIntPoint(5,10),0)-P->SpellOrigin()).GetSafeNormal();
        P->Targets[0].Actor->SetActorLocation(GridRules::Center(FIntPoint(7,10),75));
        TestTrue(TEXT("Ground trace finds conductive patch"),!P->LightningGroundPlan(P->TraceSpell(3,GroundDirection)).IsEmpty());
        P->CastLightning(GroundDirection);
        TestTrue(TEXT("Beam hitting ground electrifies it"),!P->ElectricCells.IsEmpty());
        Floor->Destroy();
        P->Cooldowns[3]=2; P->ResetArena();
        TestEqual(TEXT("Reset removes electric cells"),P->ElectricCells.Num(),0);
        TestEqual(TEXT("Reset removes beam visuals"),P->LightningEffects.Num(),0);
        TestEqual(TEXT("Reset clears fourth cooldown"),P->Cooldowns[3],0.f);
        for(const auto& T:P->Targets) TestEqual(TEXT("Reset clears enemy wet state"),T.WetRemaining,0.f);
    }
    else AddError(TEXT("Failed to create player/controller"));
    World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return !HasAnyErrors();
}
#endif
