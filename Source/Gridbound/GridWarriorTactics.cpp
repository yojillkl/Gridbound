#include "GridGame.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

bool AGridPawn::WarriorBurning(FIntPoint Cell) const
{
    for(const auto& B:BurningCells) if(B.Cell==Cell && B.Remaining>0.f) return true;
    for(const auto& E:ElectricCells) if(E.Cell==Cell && E.Remaining>0.f && IsConductiveCell(Cell)) return true;
    return false;
}

bool AGridPawn::WarriorPositionAllowed(int32 Index,FVector Position,float FromHeight) const
{
    const float Step=bLevelMode?GridRules::LevelStepHeight:GridRules::MaxStepHeight;
    // 检查角色足迹而不只是中心，防止斜穿河岸、墙角和棋盘边缘。
    const float R=GridRules::WarriorRadius;
    const float D=R*.70710678f;
    for(const FVector Offset:{FVector::ZeroVector,FVector(R,0,0),FVector(-R,0,0),FVector(0,R,0),FVector(0,-R,0),
        FVector(D,D,0),FVector(D,-D,0),FVector(-D,D,0),FVector(-D,-D,0)})
    {
        const FIntPoint C=GridRules::Cell(Position+Offset);
        if(!GridRules::Inside(C) || (bLevelMode && LevelBlocked(C)) || SurfaceHeight(C)>FromHeight+Step) return false;
    }
    return true;
}

FVector AGridPawn::AvoidWarriorBodies(int32 Index,FVector Position,FVector Delta) const
{
    const auto& T=Targets[Index];
    const double Separation=2.f*GridRules::CharacterMoveRadius;
    auto Slide=[&](FVector Other)
    {
        if(FMath::Abs(Position.Z-Other.Z)>=GridRules::MeleeReachHeight) return;
        const FVector Offset=Position-Other;
        const double Before=Offset.SizeSquared2D();
        const double After=(Offset+Delta).SizeSquared2D();
        // 已经重叠时，任何增大间距的移动都必须允许，不能把角色锁在原地。
        if(After>=Separation*Separation || (Before<Separation*Separation && After>Before)) return;
        if(T.bCharging) { Delta=FVector::ZeroVector; return; }
        const FVector Normal=Offset.GetSafeNormal2D();
        const double Length=Delta.Size2D();
        Delta-=Normal*FMath::Min(0.,FVector::DotProduct(Delta,Normal));
        if(Delta.SizeSquared2D()<Length*Length*.01)
            Delta=FVector(-Normal.Y,Normal.X,0)*Length*T.OrbitSign;
    };
    if(Health>0) Slide(GetActorLocation());
    for(int32 I=0;I<Targets.Num();++I)
        if(I!=Index && Targets[I].Health>0 && Targets[I].Actor.IsValid()) Slide(Targets[I].Actor->GetActorLocation());
    return Delta;
}

bool AGridPawn::WarriorCanTraverse(int32 Index,FVector Start,FVector End,bool bAvoidFire) const
{
    if(!Targets.IsValidIndex(Index)) return false;
    FCollisionQueryParams Params; Params.AddIgnoredActor(this); Params.AddIgnoredActor(Targets[Index].Actor.Get());
    // 模型的方形命中盒只服务于法术，角色间行走由软避让处理。
    for(const auto& Other:Targets) if(Other.Actor.IsValid()) Params.AddIgnoredActor(Other.Actor.Get());
    const int32 Steps=FMath::Max(1,FMath::CeilToInt(FVector::Dist2D(Start,End)/20.f));
    FVector Previous=Start;
    float Height=Targets[Index].FootHeight;
    for(int32 S=1;S<=Steps;++S)
    {
        FVector P=FMath::Lerp(Start,End,float(S)/Steps);
        if(!WarriorPositionAllowed(Index,P,Height)) return false;
        const FIntPoint C=GridRules::Cell(P);
        if(bAvoidFire && WarriorBurning(C) && SurfaceHeight(C)<GridRules::GroundTolerance
            && C!=GridRules::Cell(Start)) return false;
        Height=FMath::Max(Height,SurfaceHeight(C));
        Previous.Z=P.Z=Height+GridRules::ActorOriginHeight;
        FHitResult Hit;
        if(GetWorld()->SweepSingleByChannel(Hit,Previous,P,FQuat::Identity,ECC_Visibility,
            FCollisionShape::MakeSphere(GridRules::WarriorRadius),Params)) return false;
        Previous=P;
    }
    return true;
}

bool AGridPawn::MoveWarriorContinuous(int32 Index,FVector Goal,float DT,float Speed)
{
    auto& T=Targets[Index];
    const FVector Start=T.Actor->GetActorLocation();
    const FVector Direction=(Goal-Start).GetSafeNormal2D();
    const float Travel=FMath::Min(float(FVector::Dist2D(Start,Goal)),Speed*FMath::Max(0.f,DT)
        *MovementSpeedMultiplier(T.Cell,T.FootHeight));
    const int32 Steps=FMath::Max(1,FMath::CeilToInt(Travel/18.f));
    FCollisionQueryParams Params; Params.AddIgnoredActor(this); Params.AddIgnoredActor(T.Actor.Get());
    for(const auto& Other:Targets) if(Other.Actor.IsValid()) Params.AddIgnoredActor(Other.Actor.Get());
    FVector Position=Start;
    for(int32 S=0;S<Steps;++S)
    {
        FVector Remaining=AvoidWarriorBodies(Index,Position,Direction*(Travel/Steps));
        for(int32 Contact=0;Contact<3 && !Remaining.IsNearlyZero();++Contact)
        {
            FVector End=Position+Remaining;
            if(!WarriorPositionAllowed(Index,End,T.FootHeight)) break;
            const float Support=SurfaceHeight(GridRules::Cell(End));
            FVector SweepStart=Position;
            SweepStart.Z=End.Z=FMath::Max(T.FootHeight,Support)+GridRules::ActorOriginHeight;
            FHitResult Hit;
            if(!GetWorld()->SweepSingleByChannel(Hit,SweepStart,End,FQuat::Identity,ECC_Visibility,
                FCollisionShape::MakeSphere(GridRules::WarriorRadius),Params))
            { Position=End; T.FootHeight=FMath::Max(T.FootHeight,Support); break; }
            if(Hit.bStartPenetrating || T.bCharging) break;
            const FVector Normal=Hit.Normal.GetSafeNormal2D();
            const FVector ContactPosition=Hit.Location+Normal*.1f;
            if(!WarriorPositionAllowed(Index,ContactPosition,T.FootHeight)) break;
            Position=ContactPosition;
            Remaining*=1.f-Hit.Time;
            Remaining-=Normal*FMath::Min(0.f,FVector::DotProduct(Remaining,Normal));
        }
    }
    T.Actor->SetActorLocation(Position);
    T.Cell=GridRules::Cell(Position); T.MoveDestination=T.Cell;
    T.bWalking=FVector::DistSquared2D(Start,Position)>.01f;
    if(T.bWalking)
    {
        const FVector Facing=T.bSeesPlayer && !T.bCharging?GetActorLocation()-Position:Direction;
        T.Actor->SetActorRotation(FMath::RInterpTo(T.Actor->GetActorRotation(),FRotator(0,Facing.Rotation().Yaw,0),DT,14.f));
    }
    return T.bWalking;
}

bool AGridPawn::ChooseWarriorEscape(int32 Index,FVector& Goal,bool bAwayFromPlayer) const
{
    const auto& T=Targets[Index];
    const FVector Start=T.Actor->GetActorLocation();
    float Best=-MAX_flt;
    bool bFound=false;
    for(float Radius:{180.f,300.f,450.f}) for(int32 I=0;I<16;++I)
    {
        const float A=I*2.f*PI/16.f;
        const FVector P=Start+FVector(FMath::Cos(A),FMath::Sin(A),0)*Radius;
        if(WarriorBurning(GridRules::Cell(P)) || !WarriorCanTraverse(Index,Start,P)) continue;
        const float Score=(bAwayFromPlayer?float(FVector::Dist2D(P,T.LastKnownPosition)):0.f)-Radius*.3f;
        if(Score>Best) { Best=Score; Goal=P; bFound=true; }
    }
    return bFound;
}

bool AGridPawn::BeginWarriorCharge(int32 Index)
{
    auto& T=Targets[Index];
    const FVector Start=T.Actor->GetActorLocation();
    const float Distance=FVector::Dist2D(Start,GetActorLocation());
    if(!T.bSeesPlayer || T.TacticalShiftRemaining>0.f || T.ChargeCooldown>0.f || Distance<300.f || Distance>GridRules::ChargeRange
        || FMath::Abs(T.FootHeight-FootHeight)>GridRules::MeleeReachHeight) return false;
    // 锁定一次有限预判，预警后不追踪转弯；横向闪开即可躲过。
    FVector Aim=GetActorLocation()+T.ObservedPlayerVelocity.GetClampedToMaxSize2D(520.f)*.25f;
    Aim.Z=Start.Z;
    const FVector Direction=(Aim-Start).GetSafeNormal2D();
    const FVector End=Start+Direction*FMath::Min(float(FVector::Dist2D(Start,Aim))-125.f,750.f);
    if(!WarriorCanTraverse(Index,Start,End)) return false;
    T.Skill=EWarriorSkill::Charge; T.SkillWindupDuration=.55f; T.Windup=.55f;
    T.ChargeCooldown=GridRules::ChargeCooldown; T.ChargeEnd=End; T.StrikeDirection=Direction;
    T.StrikeCell=GridRules::Cell(Aim); T.bStrikeWall=false; T.bWalking=false;
    T.StrikesRemaining=0; T.bSurgeFollowup=false;
    T.Actor->SetActorRotation(FRotator(0,Direction.Rotation().Yaw,0));
    return true;
}

bool AGridPawn::UseWarriorSecondWind(int32 Index)
{
    if(!Targets.IsValidIndex(Index)) return false;
    auto& T=Targets[Index];
    if(T.Health<=0 || !T.Actor.IsValid() || T.HoldRemaining>0.f || T.bCharging
        || (T.Skill==EWarriorSkill::Charge && T.Windup>0.f) || Health<=0 || bLevelComplete
        || T.Health>=T.MaxHealth || T.SecondWindUses<=0 || T.SecondWindCooldown>0.f) return false;
    --T.SecondWindUses;
    T.SecondWindCooldown=GridRules::RoundSeconds;
    const int32 Roll=RollDice(1,10),Healing=FMath::Min(T.MaxHealth-T.Health,Roll+T.Level);
    T.Health+=Healing;
    T.ReleasedSkill=EWarriorSkill::SecondWind;
    T.SkillRecoveryDuration=.65f; T.SkillRecoveryRemaining=.65f;
    T.TacticalShiftRemaining=0.f;
    if(ChooseWarriorEscape(Index,T.TacticalShiftGoal,true))
        T.TacticalShiftRemaining=FMath::Min(450.f,float(FVector::Dist2D(T.Actor->GetActorLocation(),T.TacticalShiftGoal)));
    Feedback=FString::Printf(TEXT("战士回气：恢复 %d 生命 · 剩余 %d 次"),Healing,T.SecondWindUses);
    AddCombatReadout(false,TEXT("回气"),FString::Printf(TEXT("d10(%d) +%d = %d"),Roll,T.Level,Roll+T.Level),
        FString::Printf(TEXT("恢复 %d 生命 · 剩余 %d 次"),Healing,T.SecondWindUses),Roll+T.Level,10);
    return true;
}

void AGridPawn::RememberWarriorPlayer(FTarget& T,FVector Position)
{
    bEncounterStarted=true;
    T.LastKnownPosition=Position; T.LastKnownPlayer=GridRules::Cell(Position);
    T.AlertTime=GridRules::EnemyMemorySeconds+GridRules::EnemySearchSeconds;
    T.bSearchingArea=false; T.bHasSearchGoal=false;
    T.SearchRemaining=GridRules::EnemySearchSeconds; T.SearchStep=0;
}

bool AGridPawn::ChooseWarriorSearchGoal(int32 Index)
{
    auto& T=Targets[Index];
    // 优先延续最后看见的移动方向，再检查两侧与更远的可达位置。
    const float Angles[]={0.f,45.f,-45.f,90.f,-90.f,180.f};
    for(int32 Attempt=0;Attempt<18;++Attempt)
    {
        const int32 Step=T.SearchStep++%18;
        const FVector Direction=T.LastSeenDirection.RotateAngleAxis(Angles[Step%6],FVector::UpVector);
        const FVector Candidate=T.LastKnownPosition+Direction*(2.f+Step/6)*GridRules::CellSize;
        const FIntPoint Goal=GridRules::Cell(Candidate);
        if(!GridRules::Inside(Goal) || (bLevelMode && LevelBlocked(Goal)) || WarriorBurning(Goal)) continue;
        FIntPoint Next;
        if(!EnemyNextStep(Index,false,Next,&Goal,nullptr,false)) continue;
        T.SearchGoal=GridRules::Center(Goal,SurfaceHeight(Goal)+GridRules::ActorOriginHeight);
        T.bHasSearchGoal=true;
        return true;
    }
    T.bHasSearchGoal=false;
    return false;
}

void AGridPawn::TickWarriorBrain(int32 Index,float DT)
{
    auto& T=Targets[Index];
    if(T.Health<=0 || !T.Actor.IsValid()) return;
    if(T.HoldRemaining>0.f)
    {
        InterruptHeldTarget(T);
        return;
    }
    T.Cell=GridRules::Cell(T.Actor->GetActorLocation());
    for(float* Timer:{&T.AttackCooldown,&T.ThinkTime,&T.SlashRemaining,&T.ChargeCooldown,
        &T.TripCooldown,&T.DodgeCooldown,&T.TacticRemaining,&T.SkillRecoveryRemaining,&T.SecondWindCooldown}) *Timer=FMath::Max(0.f,*Timer-DT);
    T.AlertTime=FMath::Max(0.f,T.AlertTime-DT);
    if(T.bSearchingArea)
    {
        T.SearchRemaining=FMath::Max(0.f,T.SearchRemaining-DT);
        if(T.SearchRemaining<=0.f) T.AlertTime=0.f;
    }
    T.SenseTime-=DT;
    if(T.SenseTime<=0.f)
    {
        const float SampleTime=.1f-T.SenseTime;
        T.SenseTime=.1f;
        const bool bPreviouslyVisible=T.bSeesPlayer;
        T.bSeesPlayer=EnemySeesPlayer(T);
        if(T.bSeesPlayer!=bPreviouslyVisible)
        { T.ThinkTime=0.f; T.TacticRemaining=0.f; }
        if(T.bSeesPlayer)
        {
            const FVector Position=GetActorLocation();
            T.ObservedPlayerVelocity=T.bHasObservation?(Position-T.LastKnownPosition)/SampleTime:FVector::ZeroVector;
            T.ObservedPlayerVelocity.Z=0;
            if(T.ObservedPlayerVelocity.SizeSquared2D()>100.f)
                T.LastSeenDirection=T.ObservedPlayerVelocity.GetSafeNormal2D();
            else if(!T.bHasObservation)
                T.LastSeenDirection=(Position-T.Actor->GetActorLocation()).GetSafeNormal2D();
            RememberWarriorPlayer(T,Position);
            T.bHasObservation=true;
        }
        else { T.bHasObservation=false; T.ObservedPlayerVelocity=FVector::ZeroVector; }
    }
    if(T.StaggerRemaining>0.f)
    {
        T.StaggerRemaining=FMath::Max(0.f,T.StaggerRemaining-DT);
        T.bWalking=false; T.bCharging=false; T.Windup=0; T.SkillRecoveryRemaining=0; T.Intent=EWarriorIntent::Recover;
        AnimateWarrior(T,DT); return;
    }
    // Second Wind is a bonus action: it does not cancel or replace the attack action.
    if(T.bDuelist && !T.bCharging && T.AlertTime>0.f && T.Health<=T.MaxHealth*.55f)
        UseWarriorSecondWind(Index);
    const bool bShifting=T.TacticalShiftRemaining>0.f && !T.bCharging;
    if(bShifting)
    {
        const FVector Before=T.Actor->GetActorLocation();
        const bool Moved=MoveWarriorContinuous(Index,T.TacticalShiftGoal,
            FMath::Min(DT,T.TacticalShiftRemaining/GridRules::WarriorSpeed),GridRules::WarriorSpeed);
        T.TacticalShiftRemaining=Moved?FMath::Max(0.f,T.TacticalShiftRemaining-float(FVector::Dist2D(Before,T.Actor->GetActorLocation()))):0.f;
        if(FVector::Dist2D(T.Actor->GetActorLocation(),T.TacticalShiftGoal)<8.f) T.TacticalShiftRemaining=0.f;
    }
    if(T.Windup>0.f)
    {
        T.bWalking=false;
        const FColor Color=T.Skill==EWarriorSkill::SecondWind?FColor::Green:
            T.Skill==EWarriorSkill::Trip?FColor::Purple:T.Skill==EWarriorSkill::Push?FColor::Red:
            T.Skill==EWarriorSkill::Charge?FColor::Cyan:FColor::Orange;
        if(T.Skill==EWarriorSkill::Charge)
        {
            const FVector Start=T.Actor->GetActorLocation()-FVector(0,0,65);
            const FVector End=T.ChargeEnd-FVector(0,0,65);
            const FVector Side=FVector(-T.StrikeDirection.Y,T.StrikeDirection.X,0)*GridRules::WarriorRadius;
            DrawDebugLine(GetWorld(),Start+Side,End+Side,Color,false,0,0,3);
            DrawDebugLine(GetWorld(),Start-Side,End-Side,Color,false,0,0,3);
        }
        else DrawCell(T.Skill==EWarriorSkill::SecondWind?T.Cell:T.StrikeCell,Color);
        T.Windup=FMath::Max(0.f,T.Windup-DT);
        if(T.Windup<=0.f) ResolveWarriorSkill(Index);
        AnimateWarrior(T,DT); return;
    }
    if(T.bCharging)
    {
        const float TravelTime=FMath::Min(DT,T.ChargeRemaining);
        const bool bMoved=MoveWarriorContinuous(Index,T.ChargeEnd,TravelTime,GridRules::ChargeSpeed);
        T.ChargeRemaining=FMath::Max(0.f,T.ChargeRemaining-DT);
        if(!bMoved || T.ChargeRemaining<=0.f || FVector::Dist2D(T.Actor->GetActorLocation(),T.ChargeEnd)<8.f)
        {
            T.bCharging=false; T.bWalking=false; T.Skill=EWarriorSkill::Slash;
            T.ReleasedSkill=EWarriorSkill::Charge; T.SkillRecoveryDuration=.3f; T.SkillRecoveryRemaining=.3f;
            // Dash spends the attack action; reaching the player never grants a free strike.
            T.AttackCooldown=GridRules::SlashCooldown; T.ThinkTime=0;
        }
        AnimateWarrior(T,DT); return;
    }
    const FVector Position=T.Actor->GetActorLocation();
    const float Distance=FVector::Dist2D(Position,T.LastKnownPosition);
    if(T.ThinkTime<=0.f)
    {
        T.ThinkTime=.12f; T.bWalking=false; T.bPathBlocked=false;
        const bool bInFire=T.FootHeight<GridRules::GroundTolerance && WarriorBurning(T.Cell);
        bool bChosen=false;
        if(T.StuckTime>=1.f)
        {
            T.StuckTime=0.f; T.TacticRemaining=0.f; T.bHasSearchGoal=false;
            if(ChooseWarriorEscape(Index,T.MoveGoal,false))
            { T.Intent=EWarriorIntent::Flank; T.TacticRemaining=.65f; bChosen=true; }
        }
        if(bInFire && ChooseWarriorEscape(Index,T.MoveGoal,true))
        { T.Intent=EWarriorIntent::Retreat; T.TacticRemaining=.3f; bChosen=true; }
        // 只对正在靠近且即将穿过自身的可见火球做侧闪，有反应间隔。
        if(!bChosen && T.bSeesPlayer && T.DodgeCooldown<=0.f)
            for(const auto& F:Fireballs) if(F.Actor.IsValid())
            {
                const FVector To=Position-F.Actor->GetActorLocation();
                const float Along=FVector::DotProduct(To,F.Direction);
                const FVector Miss=To-F.Direction*Along;
                if(Along<160.f || Along>700.f || Miss.Size()>100.f) continue;
                const FVector Side=FVector(-F.Direction.Y,F.Direction.X,0).GetSafeNormal2D();
                for(float Sign:{T.OrbitSign,-T.OrbitSign})
                {
                    const FVector Goal=Position+Side*Sign*190.f;
                    if(!WarriorCanTraverse(Index,Position,Goal)) continue;
                    T.MoveGoal=Goal; T.TacticRemaining=.42f; T.DodgeCooldown=2.2f;
                    T.Intent=EWarriorIntent::Flank; bChosen=true; break;
                }
                if(bChosen) break;
            }
        if(!bChosen && T.TacticRemaining>0.f) bChosen=true;
        if(!bChosen && T.bSeesPlayer)
        {
            const bool bMelee=Distance<=GridRules::CellSize+GridRules::MeleeReachExtra
                && FMath::Abs(T.FootHeight-FootHeight)<=GridRules::MeleeReachHeight;
            if(T.AttackCooldown<=0.f && EnemyAttackSpacing<=0.f && bMelee)
            {
                BeginWarriorAttack(T,GridRules::Cell(GetActorLocation()),false);
                EnemyAttackSpacing=GridRules::AttackSpacing;
                AnimateWarrior(T,DT); return;
            }
            if(T.bDuelist && T.AttackCooldown<=0.f && BeginWarriorCharge(Index))
            { AnimateWarrior(T,DT); return; }
            const FVector Toward=(T.LastKnownPosition-Position).GetSafeNormal2D();
            if(Distance<280.f && T.AttackCooldown>.2f)
            {
                // 出手后的恢复期沿玩家侧面移动；贴得过近时同时后撤。
                const FVector Side(-Toward.Y,Toward.X,0);
                T.MoveGoal=Position+Side*T.OrbitSign*110.f+Toward*(Distance<180.f?-100.f:0.f);
                if(!WarriorCanTraverse(Index,Position,T.MoveGoal))
                { T.OrbitSign*=-1.f; T.MoveGoal=Position+Side*T.OrbitSign*110.f-Toward*70.f; }
                T.Intent=EWarriorIntent::Flank;
            }
            else
            {
                const FVector Lead=T.ObservedPlayerVelocity.GetClampedToMaxSize2D(520.f)*FMath::Clamp(Distance/1800.f,0.f,.3f);
                T.MoveGoal=T.LastKnownPosition+Lead-Toward*125.f;
                T.Intent=EWarriorIntent::Chase;
            }
            bChosen=true;
        }
        if(!bChosen)
        {
            if(T.AlertTime>0.f)
            {
                T.Intent=EWarriorIntent::Search;
                if(!T.bSearchingArea && (Distance<100.f || T.AlertTime<=GridRules::EnemySearchSeconds))
                {
                    T.bSearchingArea=true; T.SearchRemaining=GridRules::EnemySearchSeconds;
                }
                if(T.bSearchingArea && (!T.bHasSearchGoal || FVector::Dist2D(Position,T.SearchGoal)<60.f))
                    ChooseWarriorSearchGoal(Index);
                T.MoveGoal=T.bSearchingArea?(T.bHasSearchGoal?T.SearchGoal:Position):T.LastKnownPosition;
            }
            else
            {
                T.bSearchingArea=false; T.bHasSearchGoal=false;
                T.Intent=EWarriorIntent::Patrol;
                const float Angle=(int32(LevelSeconds/3.f)%4)*PI*.5f;
                T.MoveGoal=GridRules::Center(T.HomeCell,T.FootHeight+GridRules::ActorOriginHeight)
                    +FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*100.f;
            }
        }
        if(!WarriorCanTraverse(Index,Position,T.MoveGoal))
        {
            const FIntPoint Goal=GridRules::Cell(T.MoveGoal);
            FIntPoint Next,Reachable;
            if(EnemyNextStep(Index,true,Next,&Goal,&Reachable))
            {
                if(SurfaceHeight(Next)>T.FootHeight+(bLevelMode?GridRules::LevelStepHeight:GridRules::MaxStepHeight))
                {
                    const FVector WallCenter=GridRules::Center(Next,T.FootHeight+GridRules::ActorOriginHeight);
                    if(FVector::Dist2D(Position,WallCenter)<=GridRules::CellSize+GridRules::MeleeReachExtra)
                    {
                        T.MoveGoal=Position;
                        if(T.AttackCooldown<=0.f) BeginWarriorAttack(T,Next,true);
                    }
                    else T.MoveGoal=WallCenter-(WallCenter-Position).GetSafeNormal2D()*145.f;
                }
                else
                {
                    T.MoveGoal=GridRules::Center(Next,T.FootHeight+GridRules::ActorOriginHeight);
                    // 世界位置可能刚跨过格界；绕墙拐弯前先给身体留够转弯空间。
                    if(!WarriorCanTraverse(Index,Position,T.MoveGoal))
                        T.MoveGoal=GridRules::Center(T.Cell,T.FootHeight+GridRules::ActorOriginHeight);
                }
            }
            else
            {
                T.bPathBlocked=FVector::Dist2D(Position,T.MoveGoal)>35.f;
                T.MoveGoal=Position;
                if(T.Intent==EWarriorIntent::Search)
                {
                    if(!T.bSearchingArea) { T.bSearchingArea=true; T.SearchRemaining=GridRules::EnemySearchSeconds; }
                    T.bHasSearchGoal=false;
                }
            }
        }
    }
    if(T.Windup<=0.f && !bShifting)
    {
        const bool bWantsMove=T.bPathBlocked || FVector::Dist2D(Position,T.MoveGoal)>35.f;
        MoveWarriorContinuous(Index,T.MoveGoal,DT,GridRules::WarriorSpeed);
        const float Progress=FVector::Dist2D(Position,T.Actor->GetActorLocation());
        T.StuckTime=bWantsMove && Progress<FMath::Min(2.f,GridRules::WarriorSpeed*DT*.1f)?T.StuckTime+DT:0.f;
    }
    AnimateWarrior(T,DT);
}
