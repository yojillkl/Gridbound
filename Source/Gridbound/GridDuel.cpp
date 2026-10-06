#include "GridGame.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"

// Level five: two attacks per action, one additional action from Action Surge.
void AGridPawn::BeginWarriorAttack(FTarget& T,FIntPoint Cell,bool bWall)
{
    T.StrikeCell=Cell; T.bStrikeWall=bWall; T.Skill=EWarriorSkill::Slash;
    T.AnimationStrikeNumber=0;
    T.bWalking=false; T.StrikesRemaining=T.bDuelist && !bWall?1:0;
    T.bSurgeFollowup=false; T.SkillWindupDuration=.6f;
    if(T.bDuelist && !bWall)
    {
        if(!T.bActionSurgeUsed && (TripRemaining>0.f || T.Health<=T.MaxHealth*.7f))
        {
            T.Skill=EWarriorSkill::ActionSurge; T.bActionSurgeUsed=true;
            T.StrikesRemaining=3; T.bSurgeFollowup=true; T.SkillWindupDuration=.7f;
            AddCombatReadout(false,TEXT("动作如潮"),TEXT("额外动作 +1"),TEXT("本轮最多 4 次攻击"),1);
        }
        else if(T.SuperiorityDice>0 && T.TripCooldown<=0.f)
        {
            T.Skill=!T.bPreferPush && IsGrounded() && TripRemaining<=0.f?EWarriorSkill::Trip:EWarriorSkill::Push;
            T.SkillWindupDuration=.8f;
        }
    }
    const FVector End=bWall?GridRules::Center(Cell,T.FootHeight+GridRules::ActorOriginHeight):GetActorLocation();
    T.StrikeDirection=(End-T.Actor->GetActorLocation()).GetSafeNormal2D();
    T.Actor->SetActorRotation(FRotator(0,T.StrikeDirection.Rotation().Yaw,0));
    T.Windup=T.SkillWindupDuration;
}

void AGridPawn::ResolveWarriorSkill(int32 Index)
{
    if(!Targets.IsValidIndex(Index)) return;
    auto& T=Targets[Index];
    if(T.Health<=0 || !T.Actor.IsValid() || Health<=0 || bLevelComplete || T.HoldRemaining>0.f) return;
    // Keep the released action separately: combo scheduling immediately changes T.Skill.
    T.ReleasedSkill=T.Skill; T.ReleasedStrikeNumber=T.AnimationStrikeNumber;
    T.SkillRecoveryDuration=T.Skill==EWarriorSkill::SecondWind?.65f:.3f;
    T.SkillRecoveryRemaining=T.SkillRecoveryDuration;
    if(T.Skill==EWarriorSkill::Charge)
    {
        T.bCharging=true;
        T.ChargeRemaining=FVector::Dist2D(T.Actor->GetActorLocation(),T.ChargeEnd)/GridRules::ChargeSpeed+.15f;
        AddCombatReadout(false,TEXT("疾走"),FString::Printf(TEXT("速度 %.1f 米 / 秒"),GridRules::ChargeSpeed/100.f),
            TEXT("本次动作不攻击"),FMath::RoundToInt(GridRules::ChargeSpeed/100.f));
        return;
    }
    if(T.Skill==EWarriorSkill::SecondWind)
    {
        UseWarriorSecondWind(Index);
        T.Skill=EWarriorSkill::Slash;
        return;
    }
    int32 WallIndex=INDEX_NONE;
    if(T.bStrikeWall) for(int32 W=0;W<Walls.Num();++W)
        if(Walls[W].Cell==T.StrikeCell) { WallIndex=W; break; }
    const bool bLockedTarget=(T.bStrikeWall && WallIndex!=INDEX_NONE)
        || (!T.bStrikeWall && GridRules::Cell(GetActorLocation())==T.StrikeCell);
    const bool bTripMiss=T.Skill==EWarriorSkill::Trip && !IsGrounded();
    const int32 PreviousSerial=CombatReadoutSerial,PreviousPending=PendingAttacks.Num();
    if(bLockedTarget && !bTripMiss) TryWarriorSlash(Index,WallIndex);
    if(!T.bStrikeWall && CombatReadoutSerial==PreviousSerial && PendingAttacks.Num()==PreviousPending)
        AddCombatReadout(false,TEXT("长剑攻击"),TEXT("挥空 · 0 伤害"),TEXT("目标已离开攻击范围"),0);
    T.SlashRemaining=.25f;
    // 每一击都有独立的预警；玩家脱离近战后取消余下连击并重新追击。
    if(T.StrikesRemaining>0 && Health>0 && T.bSeesPlayer
        && FVector::Dist2D(T.Actor->GetActorLocation(),GetActorLocation())<=GridRules::CellSize+GridRules::MeleeReachExtra
        && FMath::Abs(T.FootHeight-FootHeight)<=GridRules::MeleeReachHeight)
    {
        --T.StrikesRemaining;
        ++T.AnimationStrikeNumber;
        if(T.Skill!=EWarriorSkill::ActionSurge) T.Skill=EWarriorSkill::Slash;
        T.StrikeCell=GridRules::Cell(GetActorLocation());
        T.StrikeDirection=(GetActorLocation()-T.Actor->GetActorLocation()).GetSafeNormal2D();
        T.Actor->SetActorRotation(FRotator(0,T.StrikeDirection.Rotation().Yaw,0));
        T.SkillWindupDuration=.5f; T.Windup=.5f; T.AttackCooldown=0.f;
        T.bSurgeFollowup=T.StrikesRemaining>0;
    }
    else
    {
        T.Skill=EWarriorSkill::Slash; T.StrikesRemaining=0; T.bSurgeFollowup=false;
        T.AttackCooldown=1.f; T.ThinkTime=0; T.OrbitSign*=-1.f;
    }
}

FString AGridPawn::WarriorSkillHint(const FTarget& T) const
{
    if(T.HoldRemaining>0.f) return FString::Printf(TEXT("麻痹 · 下次感知豁免 %.1f 秒"),T.HoldSaveRemaining);
    if(T.StaggerRemaining>0.f) return TEXT("硬直中：趁机拉开距离或反击");
    if(T.TacticalShiftRemaining>0.f) return TEXT("战术转移：回气后移动");
    if(T.bCharging) return TEXT("疾走接近：本次动作不攻击");
    if(T.Windup<=0.f)
    {
        if(T.Intent==EWarriorIntent::Retreat) return TEXT("正在撤离危险区域");
        if(T.Intent==EWarriorIntent::Flank) return TEXT("正在侧移：寻找进攻位置");
        if(T.Intent==EWarriorIntent::Search) return TEXT("正在搜索：利用掩体重新拉开距离");
        return TEXT("正在追击：准备两连击");
    }
    switch(T.Skill)
    {
    case EWarriorSkill::SecondWind: return TEXT("回气：恢复 1d10+5 生命");
    case EWarriorSkill::ActionSurge: return TEXT("动作如潮：四连击！移出预警格");
    case EWarriorSkill::Charge: return TEXT("疾走：青色路线已锁定");
    case EWarriorSkill::Trip: return TEXT("绊摔攻击：命中后力量豁免 DC 15");
    case EWarriorSkill::Push: return TEXT("推动攻击：命中后力量豁免 DC 15");
    default: return TEXT("额外攻击：两次挥剑，持续移动躲避");
    }
}
void AGridPawn::TickPlayerControlEffects(float DeltaSeconds)
{
    const float DT=FMath::Max(0.f,DeltaSeconds);
    TripRemaining=FMath::Max(0.f,TripRemaining-DT);
    if(Health<=0 || bLevelComplete)
    { TripRemaining=0; KnockbackVelocity=FVector::ZeroVector; return; }
    if(KnockbackVelocity.IsNearlyZero()) return;
    const float Decay=FMath::Exp(-6.f*DT);
    const FVector Delta=KnockbackVelocity*((1.f-Decay)/6.f);
    KnockbackVelocity*=Decay;
    if(KnockbackVelocity.SizeSquared()<1.f) KnockbackVelocity=FVector::ZeroVector;
    FCollisionQueryParams Params; Params.AddIgnoredActor(this);
    for(const auto& T:Targets) if(T.Actor.IsValid()) Params.AddIgnoredActor(T.Actor.Get());
    const int32 Steps=FMath::Max(1,FMath::CeilToInt(Delta.Size()/20.f));
    FVector Position=GetActorLocation();
    for(int32 I=0;I<Steps;++I)
    {
        const FVector End=Position+Delta/Steps;
        const FIntPoint Cell=GridRules::Cell(End);
        // 击退不能穿墙、过河或把玩家直接推到高台上。
        if(!GridRules::Inside(Cell) || (bLevelMode && LevelBlocked(Cell))
            || SurfaceHeight(Cell)>FootHeight+GridRules::MaxStepHeight)
        { KnockbackVelocity=FVector::ZeroVector; break; }
        bool bBodyBlocked=false;
        for(const auto& T:Targets) if(T.Health>0 && T.Actor.IsValid()
            && FMath::Abs(End.Z-T.Actor->GetActorLocation().Z)<GridRules::MeleeReachHeight
            && FVector::Dist2D(End,T.Actor->GetActorLocation())<2.f*GridRules::CharacterMoveRadius
            && FVector::DistSquared2D(End,T.Actor->GetActorLocation())<=FVector::DistSquared2D(Position,T.Actor->GetActorLocation()))
            bBodyBlocked=true;
        if(bBodyBlocked) { KnockbackVelocity=FVector::ZeroVector; break; }
        FHitResult Hit;
        if(GetWorld()->SweepSingleByChannel(Hit,Position,End,FQuat::Identity,ECC_Visibility,
            FCollisionShape::MakeSphere(GridRules::CharacterMoveRadius),Params))
        {
            if(!Hit.bStartPenetrating) Position=Hit.Location;
            KnockbackVelocity=FVector::ZeroVector; break;
        }
        Position=End;
    }
    SetActorLocation(Position); PlayerWorldPosition=Position;
    CurrentCell=GridRules::Cell(Position); Destination=CurrentCell;
}
