#include "GridGame.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/DamageEvents.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWarriorDuelTest,"Gridbound.Combat.WarriorDuel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWarriorDuelTest::RunTest(const FString& Parameters)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    if(!TestNotNull(TEXT("World"),World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    auto* P=World->SpawnActor<AGridPawn>();
    if(P)
    {
        using Skill=AGridPawn::EWarriorSkill;
        // 逐角度调用真正的敌人移动器，验证不是八方向近似，并且斜向不加速。
        P->SpawnWarrior(FIntPoint(7,10));
        const FVector Origin(1700,1700,75);
        for(int32 Angle=0;Angle<360;++Angle)
        {
            auto& T=P->Targets[0]; T.Actor->SetActorLocation(Origin); T.Cell=GridRules::Cell(Origin);
            const float A=Angle*PI/180.f;
            const FVector Direction(FMath::Cos(A),FMath::Sin(A),0);
            P->MoveWarriorContinuous(0,Origin+Direction*500.f,.1f,GridRules::WarriorSpeed);
            TestTrue(FString::Printf(TEXT("Enemy continuous heading %d"),Angle),
                T.Actor->GetActorLocation().Equals(Origin+Direction*39.f,.002));
        }
        auto& Moving=P->Targets[0]; Moving.Actor->SetActorLocation(Origin); Moving.Cell=GridRules::Cell(Origin);
        AActor* Obstacle=P->MakeBlock(FVector(1800,1700,75),FVector(.2f,8,5),FLinearColor::White);
        P->MoveWarriorContinuous(0,Origin+FVector(500,500,0),.5f,390.f);
        TestTrue(TEXT("Enemy slides along physical wall"),Moving.Actor->GetActorLocation().Y>1800);
        TestTrue(TEXT("Enemy cannot cross physical wall"),Moving.Actor->GetActorLocation().X<1790.f-GridRules::WarriorRadius+.2f);
        Moving.Actor->SetActorLocation(Origin); Moving.bCharging=true;
        P->MoveWarriorContinuous(0,Origin+FVector(1000,0,0),1.f,1100.f);
        TestTrue(TEXT("Large-frame charge cannot tunnel through wall"),Moving.Actor->GetActorLocation().X<1790.f-GridRules::WarriorRadius+.2f);
        Moving.bCharging=false; Obstacle->Destroy();

        // 曾经小于 72 厘米就连向外移动都被拒绝；重叠后必须能脱离。
        P->SetActorLocation(Origin+FVector(30,0,0));
        Moving.Actor->SetActorLocation(Origin); Moving.Cell=GridRules::Cell(Origin);
        P->MoveWarriorContinuous(0,Origin-FVector(300,0,0),.1f,390.f);
        TestTrue(TEXT("Enemy can leave overlapping player"),Moving.Actor->GetActorLocation().X<Origin.X-30);
        Moving.Actor->SetActorLocation(Origin); Moving.Cell=GridRules::Cell(Origin);
        P->MoveWarriorContinuous(0,Origin+FVector(300,0,0),.1f,390.f);
        TestTrue(TEXT("Close frontal contact slides sideways"),FMath::Abs(Moving.Actor->GetActorLocation().Y-Origin.Y)>5);
        TestTrue(TEXT("Soft contact increases separation"),FVector::Dist2D(Moving.Actor->GetActorLocation(),P->GetActorLocation())>30);

        P->SetActorLocation(Origin);
        Moving.Actor->SetActorLocation(Origin);
        P->MoveContinuous(.1f,FVector2D(-1,0),0);
        TestTrue(TEXT("Player can leave coincident enemy body"),P->GetActorLocation().X<Origin.X-25);
        P->SetActorLocation(FVector(450,1500,75));
        P->SpawnWarrior(GridRules::Cell(Origin));
        P->Targets[1].Actor->SetActorLocation(Origin+FVector(30,0,0));
        P->Targets[0].Actor->SetActorLocation(Origin); P->Targets[0].Cell=GridRules::Cell(Origin);
        P->MoveWarriorContinuous(0,Origin-FVector(300,0,0),.1f,390.f);
        TestTrue(TEXT("Enemy can leave another enemy hitbox"),P->Targets[0].Actor->GetActorLocation().X<Origin.X-30);
        P->Targets[1].Actor->Destroy(); P->Targets.RemoveAt(1);

        // 60 厘米通道现在可通过（直径 48），但仍不能穿过通道墙。
        AActor* LeftWall=P->MakeBlock(Origin+FVector(0,-40,0),FVector(8,.2f,5),FLinearColor::White);
        AActor* RightWall=P->MakeBlock(Origin+FVector(0,40,0),FVector(8,.2f,5),FLinearColor::White);
        P->Targets[0].Actor->SetActorLocation(Origin); P->Targets[0].Cell=GridRules::Cell(Origin);
        P->MoveWarriorContinuous(0,Origin+FVector(300,0,0),.3f,390.f);
        TestTrue(TEXT("Enemy passes narrow corridor"),P->Targets[0].Actor->GetActorLocation().X>Origin.X+110);
        P->SetActorLocation(Origin); P->MoveContinuous(.3f,FVector2D(1,0),0);
        TestTrue(TEXT("Player passes narrow corridor"),P->GetActorLocation().X>Origin.X+110);
        LeftWall->Destroy(); RightWall->Destroy();

        P->bLevelMode=true; P->ResetArena();
        TestEqual(TEXT("Exactly one level five enemy"),P->Targets.Num(),1);
        TestEqual(TEXT("Fighter level"),P->Targets[0].Level,5);
        TestEqual(TEXT("Fixed level five health"),P->Targets[0].Health,49);
        TestTrue(TEXT("Level five attack and saving throws"),P->Targets[0].AttackBonus==7
            && P->Targets[0].ConstitutionSave==6 && P->Targets[0].WisdomSave==1 && P->Targets[0].DexteritySave==1);
        auto Ready=[P]()
        {
            P->StartEncounter(); P->RespawnPlayer(FIntPoint(6,10));
            P->ReactionRemaining=999.f; P->CombatRandom.Initialize(177);
            auto& T=P->Targets[0]; T.AttackCooldown=0; T.AlertTime=8;
            T.LastKnownPlayer=P->CurrentCell; T.LastKnownPosition=P->GetActorLocation();
            T.ThinkTime=0; P->EnemyAttackSpacing=0;
        };
        Ready();
        P->TickLevelCombatants(.01f);
        TestTrue(TEXT("Grounded target invites trip"),P->Targets[0].Skill==Skill::Trip);
        TestEqual(TEXT("Windup does not spend a die"),P->Targets[0].SuperiorityDice,4);
        TestEqual(TEXT("Windup has no instant damage"),P->Health,GridRules::MaxHealth);
        P->TickLevelCombatants(.81f);
        TestTrue(TEXT("Landed maneuver spends one die"),P->Targets[0].SuperiorityDice==3 && P->Health<GridRules::MaxHealth);

        Ready(); P->TickLevelCombatants(.01f); P->TryJump();
        P->TickLevelCombatants(.81f);
        TestEqual(TEXT("Jump avoids trip"),P->Health,GridRules::MaxHealth);
        TestEqual(TEXT("Spatial miss does not spend a die"),P->Targets[0].SuperiorityDice,4);

        Ready(); P->Health=100; // Durable fixture to observe all four strikes without an early death.
        P->Targets[0].SuperiorityDice=0; P->Targets[0].Health=34;
        P->TickLevelCombatants(.01f);
        TestTrue(TEXT("Action surge chosen at an opening"),P->Targets[0].Skill==Skill::ActionSurge);
        P->TickLevelCombatants(.71f);
        const int32 FirstStrikeHealth=P->Health;
        TestTrue(TEXT("Surge first strike rolls weapon damage"),FirstStrikeHealth<100);
        for(int32 N=0;N<3;++N)
        {
            TestTrue(TEXT("Each surge strike telegraphed separately"),P->Targets[0].Windup>0);
            P->TickLevelCombatants(.51f);
        }
        TestTrue(TEXT("Four attacks complete without fixed twenty damage"),P->Health<FirstStrikeHealth && P->Health>0
            && P->Targets[0].AnimationStrikeNumber==3 && P->Targets[0].Windup==0);
        TestTrue(TEXT("Surge use spent"),P->Targets[0].bActionSurgeUsed);
        TestEqual(TEXT("No healing at this threshold"),P->Targets[0].SecondWindUses,3);

        Ready(); P->Targets[0].SuperiorityDice=0; P->Targets[0].bActionSurgeUsed=true;
        P->TickLevelCombatants(.01f); P->TickLevelCombatants(.61f); P->TickLevelCombatants(.51f);
        TestTrue(TEXT("Extra attack delivers two ordinary strikes"),P->Health<GridRules::MaxHealth && P->Targets[0].AnimationStrikeNumber==1);
        TestEqual(TEXT("Ordinary sequence ends after two"),P->Targets[0].Windup,0.f);
        const FVector BeforeOrbit=P->Targets[0].Actor->GetActorLocation();
        P->TickLevelCombatants(.1f);
        TestTrue(TEXT("Existing lateral movement preserved"),FMath::Abs(P->Targets[0].Actor->GetActorLocation().Y-BeforeOrbit.Y)>1.f);

        Ready(); P->Targets[0].bWalking=true;
        P->TickLevelCombatants(.01f);
        TestTrue(TEXT("Can attack while previously moving"),P->Targets[0].Windup>0);
        P->SetActorLocation(GridRules::Center(FIntPoint(5,10),75));
        P->TickLevelCombatants(.81f);
        TestEqual(TEXT("Moving out avoids strike"),P->Health,GridRules::MaxHealth);
        TestEqual(TEXT("Leaving melee cancels remaining combo"),P->Targets[0].Windup,0.f);

        Ready(); P->RespawnPlayer(FIntPoint(4,9)); P->Targets[0].AttackCooldown=0; P->Targets[0].AlertTime=8;
        P->TickLevelCombatants(.01f);
        TestTrue(TEXT("Dash chosen at distance"),P->Targets[0].Skill==Skill::Charge);
        const FVector DashStart=P->Targets[0].Actor->GetActorLocation(),LockedEnd=P->Targets[0].ChargeEnd;
        P->SetActorLocation(P->GetActorLocation()+FVector(0,-200,0));
        P->TickLevelCombatants(.56f); P->TickLevelCombatants(.1f);
        TestTrue(TEXT("Dash moves in both axes"),FMath::Abs(P->Targets[0].Actor->GetActorLocation().X-DashStart.X)>1
            && FMath::Abs(P->Targets[0].Actor->GetActorLocation().Y-DashStart.Y)>1);
        TestTrue(TEXT("Dash endpoint does not track hidden input"),P->Targets[0].ChargeEnd.Equals(LockedEnd));
        P->TickLevelCombatants(2.f);
        TestTrue(TEXT("Dash spends action and never attacks on arrival"),!P->Targets[0].bCharging
            && P->Targets[0].Windup==0 && P->Targets[0].AttackCooldown==GridRules::SlashCooldown && P->Health==GridRules::MaxHealth);

        Ready(); P->Targets[0].Health=10;
        P->TickLevelCombatants(.01f);
        TestTrue(TEXT("AI uses limited Second Wind"),P->Targets[0].SecondWindUses==2 && P->Targets[0].Health>=16 && P->Targets[0].Health<=25);
        TestTrue(TEXT("Tactical shift has at most half speed distance"),P->Targets[0].TacticalShiftRemaining>0
            && P->Targets[0].TacticalShiftRemaining<=450);
        TestFalse(TEXT("Bonus action cannot repeat immediately"),P->UseWarriorSecondWind(0));
        Ready();
        for(int32 I=0;I<3;++I)
        {
            auto& T=P->Targets[0]; T.Health=1; T.SecondWindCooldown=0;
            TestTrue(TEXT("Three healing charges available"),P->UseWarriorSecondWind(0));
            TestTrue(TEXT("Heal is 1d10+5"),T.Health>=7 && T.Health<=16);
        }
        P->Targets[0].Health=1; P->Targets[0].SecondWindCooldown=0;
        TestFalse(TEXT("Fourth heal rejected"),P->UseWarriorSecondWind(0));
        P->Targets[0].SuperiorityDice=0; P->TickWarriorBrain(0,60.f);
        TestTrue(TEXT("Waiting never regenerates fighter resources"),P->Targets[0].SecondWindUses==0 && P->Targets[0].SuperiorityDice==0);
        Ready(); P->Targets[0].Health=48; P->UseWarriorSecondWind(0);
        TestEqual(TEXT("Healing capped at maximum"),P->Targets[0].Health,49);
        Ready(); P->Targets[0].Health=10; P->Targets[0].HoldRemaining=60;
        TestFalse(TEXT("Paralyzed fighter cannot heal"),P->UseWarriorSecondWind(0));
        TestEqual(TEXT("Blocked healing does not consume charge"),P->Targets[0].SecondWindUses,3);
        P->Targets[0].bCharging=true; P->Targets[0].TacticalShiftRemaining=450;
        P->InterruptHeldTarget(P->Targets[0]);
        TestTrue(TEXT("Hold cancels dash and tactical movement"),!P->Targets[0].bCharging && P->Targets[0].TacticalShiftRemaining==0);

        // Resolve the real delayed-hit path with deterministic saving throw bounds.
        Ready();
        AGridPawn::FPendingAttack Attack;
        Attack.Roll=10; Attack.Bonus=7; Attack.Damage=8; Attack.bTrip=true;
        Attack.SaveDC=100; Attack.Source=P->Targets[0].Actor;
        P->ResolvePendingAttack(Attack);
        TestTrue(TEXT("Trip adds die damage and failed strength save trips"),P->Health>=16 && P->Health<=23
            && P->TripRemaining>0 && P->Targets[0].SuperiorityDice==3);
        TestFalse(TEXT("Trip prevents jumping"),P->TryJump());
        P->TickPlayerControlEffects(1.f);
        TestTrue(TEXT("Trip control expires"),P->TryJump());
        Ready(); Attack.Source=P->Targets[0].Actor; Attack.SaveDC=-100; P->ResolvePendingAttack(Attack);
        TestTrue(TEXT("Successful strength save prevents trip but not damage"),P->Health<24 && P->TripRemaining==0);
        Ready(); Attack.Source=P->Targets[0].Actor; Attack.SaveDC=100; Attack.bTrip=false; Attack.bPush=true;
        Attack.PushDirection=FVector(-1,0,0); P->ResolvePendingAttack(Attack);
        const FVector PushStart=P->GetActorLocation(); P->TickPlayerControlEffects(3.f);
        TestTrue(TEXT("Push travels at most fifteen feet"),FMath::IsNearlyEqual(float(FVector::Dist2D(PushStart,P->GetActorLocation())),450.f,.1f));
        Ready(); Attack.Source=P->Targets[0].Actor; Attack.SaveDC=-100; P->ResolvePendingAttack(Attack);
        TestTrue(TEXT("Successful save prevents push"),P->KnockbackVelocity.IsNearlyZero());
        Ready(); Attack.Source=P->Targets[0].Actor; Attack.SaveDC=100;
        auto* PushWall=P->MakeBlock(FVector(820,1500,150),FVector(.2f,3,4),FLinearColor::White);
        P->ResolvePendingAttack(Attack); P->TickPlayerControlEffects(3.f);
        TestTrue(TEXT("Push stops at wall"),P->GetActorLocation().X>=830.f+GridRules::CharacterMoveRadius-.1f);
        PushWall->Destroy();
        Ready(); Attack.Source=P->Targets[0].Actor; P->Targets[0].SuperiorityDice=0; P->ResolvePendingAttack(Attack);
        TestTrue(TEXT("Exhausted dice cannot add damage or control"),P->Health==24 && P->KnockbackVelocity.IsNearlyZero());
        Ready(); Attack.Source=P->Targets[0].Actor; Attack.Roll=7; P->ShieldRemaining=6;
        P->ResolvePendingAttack(Attack);
        TestTrue(TEXT("Shield prevents control and does not spend a maneuver"),P->Health==GridRules::MaxHealth && P->Targets[0].SuperiorityDice==4);

        Ready(); P->ShieldRemaining=6;
        int32 PrecisionSeed=0;
        for(;PrecisionSeed<1000;++PrecisionSeed)
        {
            FRandomStream R(PrecisionSeed);
            const int32 Roll=R.RandRange(1,20); R.RandRange(1,8);
            const int32 Die=R.RandRange(1,8);
            if(Roll>1 && Roll+7<17 && Roll+7+Die>=17) break;
        }
        P->CombatRandom.Initialize(PrecisionSeed); P->Targets[0].Skill=Skill::Trip;
        TestTrue(TEXT("Precision can turn a miss into a hit"),P->QueueWarriorHit(0));
        TestTrue(TEXT("Precision consumes only one shared die and no second maneuver"),P->Targets[0].SuperiorityDice==3
            && P->Health>=20 && P->Health<=27 && P->TripRemaining==0);
        Ready(); P->ShieldRemaining=6;
        for(PrecisionSeed=0;PrecisionSeed<1000;++PrecisionSeed)
        {
            FRandomStream R(PrecisionSeed);
            const int32 Roll=R.RandRange(1,20); R.RandRange(1,8);
            const int32 Die=R.RandRange(1,8);
            if(Roll>1 && Roll+7<17 && Roll+7+8>=17 && Roll+7+Die<17) break;
        }
        P->CombatRandom.Initialize(PrecisionSeed);
        TestFalse(TEXT("Precision is not a guaranteed hit"),P->QueueWarriorHit(0));
        TestTrue(TEXT("Failed precision still spends its die"),P->Health==GridRules::MaxHealth && P->Targets[0].SuperiorityDice==3);
        auto SeedForRoll=[](int32 Wanted)
        {
            for(int32 Seed=0;Seed<1000;++Seed) { FRandomStream R(Seed); if(R.RandRange(1,20)==Wanted) return Seed; }
            return 0;
        };
        Ready(); P->CombatRandom.Initialize(SeedForRoll(1)); P->Targets[0].AttackBonus=100;
        TestFalse(TEXT("Natural one always misses"),P->QueueWarriorHit(0));
        TestEqual(TEXT("Natural one does not waste precision"),P->Targets[0].SuperiorityDice,4);
        Ready(); P->CombatRandom.Initialize(SeedForRoll(20)); P->Targets[0].SuperiorityDice=0;
        auto CriticalDice=P->CombatRandom; CriticalDice.RandRange(1,20);
        const int32 CriticalDamage=CriticalDice.RandRange(1,8)+CriticalDice.RandRange(1,8)+4;
        P->QueueWarriorHit(0);
        TestEqual(TEXT("Critical doubles weapon dice, not strength bonus"),P->Health,GridRules::MaxHealth-CriticalDamage);
        Ready(); P->Targets[0].Health=10; P->Targets[0].Windup=.6f;
        P->Targets[0].Skill=Skill::Slash; P->Targets[0].StrikesRemaining=1;
        P->UseWarriorSecondWind(0);
        TestTrue(TEXT("Bonus action preserves normal attack"),P->Targets[0].Windup==.6f && P->Targets[0].StrikesRemaining==1);
        P->Targets[0].bSeesPlayer=true;
        TestFalse(TEXT("Dash cannot stack with tactical shift"),P->BeginWarriorCharge(0));
        Ready(); P->Targets[0].bPreferPush=true;
        P->BeginWarriorAttack(P->Targets[0],P->CurrentCell,false);
        TestTrue(TEXT("AI can choose pushing attack"),P->Targets[0].Skill==Skill::Push);

        Ready(); P->RespawnPlayer(FIntPoint(4,10)); P->Targets[0].AlertTime=8;
        P->TickLevelCombatants(.01f);
        const FVector Memory=P->Targets[0].LastKnownPosition;
        AActor* Cover=P->MakeBlock(FVector(820,1500,150),FVector(.2f,8,5),FLinearColor::White);
        P->SetActorLocation(P->GetActorLocation()+FVector(0,150,0));
        P->Targets[0].Windup=0; P->Targets[0].bCharging=false;
        P->TickLevelCombatants(.11f);
        TestFalse(TEXT("Cover breaks vision"),P->Targets[0].bSeesPlayer);
        TestTrue(TEXT("Hidden target does not update memory"),P->Targets[0].LastKnownPosition.Equals(Memory));
        TestFalse(TEXT("No charge through cover"),P->BeginWarriorCharge(0));
        Cover->Destroy();

        Ready(); P->RespawnPlayer(FIntPoint(4,10));
        P->Targets[0].AlertTime=8; P->Targets[0].LastKnownPosition=P->GetActorLocation();
        P->Targets[0].LastKnownPlayer=P->CurrentCell; P->Targets[0].ChargeCooldown=30;
        AGridPawn::FWall Barrier; Barrier.Cell=FIntPoint(6,10); Barrier.Age=1;
        Barrier.Actor=P->MakeBlock(GridRules::Center(Barrier.Cell,225),FVector(1.5f,1.5f,4.5f),FLinearColor::White);
        P->Walls.Add(Barrier);
        bool bReached=false;
        for(int32 N=0;N<200;++N)
        {
            P->TickLevelCombatants(.05f);
            if(FVector::Dist2D(P->Targets[0].Actor->GetActorLocation(),P->GetActorLocation())<165.f)
            { bReached=true; break; }
        }
        TestTrue(TEXT("Continuous path rounds a wall corner and reaches melee"),bReached);
        TestEqual(TEXT("Prefer walking around wall to unnecessary demolition"),P->Walls[0].Durability,100);
        Barrier.Actor->Destroy(); P->Walls.Reset();

        Ready(); P->RespawnPlayer(FIntPoint(4,10));
        P->Targets[0].AlertTime=8; P->Targets[0].AttackCooldown=0;
        P->TickLevelCombatants(.01f); P->TickLevelCombatants(.56f);
        TestTrue(TEXT("Charge underway before new obstruction"),P->Targets[0].bCharging);
        AActor* NewWall=P->MakeBlock(FVector(880,1500,150),FVector(.2f,3,4),FLinearColor::White);
        P->TickLevelCombatants(.5f);
        TestTrue(TEXT("New wall blocks an already launched charge"),P->Targets[0].Actor->GetActorLocation().X>=890.f+GridRules::WarriorRadius);
        NewWall->Destroy();

        Ready(); P->Targets[0].Health=1;
        P->ResolveFireballImpact(P->Targets[0].Actor.Get(),P->Targets[0].Actor->GetActorLocation());
        P->TickLevel(.01f);
        TestTrue(TEXT("Killing sole warrior completes level"),P->bLevelComplete);
        P->TickCombatants(5.f);
        TestTrue(TEXT("No respawn after victory"),P->Targets.Num()==1 && P->Targets[0].Health==0);
        P->ResetArena(); P->Targets[0].Health=5; P->Targets[0].SecondWindUses=0; P->Targets[0].SuperiorityDice=0; P->Targets[0].bActionSurgeUsed=true;
        P->TakeDamage(100,FDamageEvent(),nullptr,nullptr);
        P->TickLevelCombatants(2.1f);
        TestEqual(TEXT("Death resets enemy health"),P->Targets[0].Health,49);
        TestTrue(TEXT("Death resets enemy skills"),P->Targets[0].SecondWindUses==3 && P->Targets[0].SuperiorityDice==4 && !P->Targets[0].bActionSurgeUsed && !P->Targets[0].bCharging);
        TestEqual(TEXT("Death restores player"),P->Health,GridRules::MaxHealth);
        TestFalse(TEXT("Retry is not victory"),P->bLevelComplete);
    }
    else AddError(TEXT("Could not spawn player"));
    World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return !HasAnyErrors();
}
#endif
