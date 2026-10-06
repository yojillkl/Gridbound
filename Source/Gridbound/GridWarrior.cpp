#include "GridGame.h"
#include "GridArt.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"

// 敌人 AI：生成、重生选点、寻路（Dijkstra）、占位查询、劈砍结算与每帧状态机推进。

void AGridPawn::AnimateWarrior(FTarget& T,float DT)
{
    if(!T.Actor.IsValid() || !T.Visual.IsValid() || DT<=0.f) return;
    const FVector Position=T.Actor->GetActorLocation();
    const float Travel=FVector::Dist2D(Position,T.PreviousVisualPosition);
    T.PreviousVisualPosition=Position;
    T.AnimationTime+=FMath::Min(Travel,50.f)*.045f;
    T.VisualSpeed=FMath::FInterpTo(T.VisualSpeed,FMath::Clamp(Travel/DT/330.f,0.f,1.f),DT,12.f);
    const float Duration=T.SkillWindupDuration;
    const float Windup=T.Windup>0?1.f-T.Windup/Duration:T.SlashRemaining/.25f;
    const float Swing=T.SlashRemaining>0?FMath::Sin(PI*(1.f-T.SlashRemaining/.25f)):0.f;
    GridArt::AnimateAdventurer(T.Visual.Get(),T.AnimationTime,T.VisualSpeed,FMath::Clamp(Windup,0.f,1.f),Swing,FMath::Clamp(T.StaggerRemaining/.6f,0.f,1.f));
    if(T.StaggerRemaining<=0.f)
    {
        const bool bRecovery=T.SkillRecoveryRemaining>0.f && !T.bCharging;
        const auto Skill=T.bCharging?EWarriorSkill::Charge:bRecovery?T.ReleasedSkill:T.Skill;
        GridArt::AnimateWarriorSkill(T.Visual.Get(),int32(Skill),T.Windup>0?FMath::Clamp(1.f-T.Windup/T.SkillWindupDuration,0.f,1.f):-1.f,
            bRecovery?FMath::Clamp(1.f-T.SkillRecoveryRemaining/T.SkillRecoveryDuration,0.f,1.f):-1.f,
            T.bCharging,bRecovery?T.ReleasedStrikeNumber:T.AnimationStrikeNumber);
    }
}

bool AGridPawn::SpawnWarrior(FIntPoint Cell)
{
    // 在指定格生成一名持剑敌人（冒险者模型 + 独立剑体）。
    if(!GridRules::Inside(Cell)) return false;
    FTarget T; T.Cell=Cell; T.HomeCell=Cell; T.MoveDestination=Cell; T.FootHeight=SurfaceHeight(Cell);
    T.Actor=MakeBlock(GridRules::Center(Cell,T.FootHeight+GridRules::ActorOriginHeight),FVector(.65f,.65f,1.5f),FLinearColor(.9f,.12f,.07f));
    if(!T.Actor.IsValid()) return false;
    Cast<UStaticMeshComponent>(T.Actor->GetRootComponent())->SetVisibility(false);
    auto* Visual=GridArt::CreateAdventurer(T.Actor.Get(),T.Actor->GetRootComponent(),TerrainMaterial?TerrainMaterial.Get():BaseMaterial.Get(),true);
    T.Visual=Visual;
    Visual->SetAbsolute(false,false,true); Visual->SetWorldScale3D(FVector::OneVector);
    T.PreviousVisualPosition=T.Actor->GetActorLocation();
    T.MoveGoal=T.PreviousVisualPosition; T.LastKnownPosition=T.MoveGoal;
    T.bDuelist=true; T.MaxHealth=GridRules::WarriorHealth; T.Health=T.MaxHealth;
    T.Actor->SetActorRotation(FRotator(0,180,0)); Targets.Add(T);
    return true;
}

bool AGridPawn::FindSpawnCell(FIntPoint Preferred,bool bForPlayer,FIntPoint& Result) const
{
    // 在首选格附近找一块安全、可站立的空地板：避开敌人、火焰与高台。
    int32 Best=MAX_int32;
    for(int32 X=0;X<GridRules::BoardSize;++X) for(int32 Y=0;Y<GridRules::BoardSize;++Y)
    {
        const FIntPoint C(X,Y);
        if(bLevelMode && LevelBlocked(C)) continue;
        if(SurfaceHeight(C)>0.f) continue;
        if(!bForPlayer && (C==GridRules::Cell(GetActorLocation()) || (bMoving && C==Destination))) continue;
        bool bUnsafe=false;
        for(const auto& T:Targets) if(T.Health>0 && T.Actor.IsValid()
            && (C==T.Cell || C==GridRules::Cell(T.Actor->GetActorLocation()) || (T.bWalking && C==T.MoveDestination))) { bUnsafe=true; break; }
        for(const auto& B:BurningCells) if(B.Remaining>0.f && B.Cell==C) { bUnsafe=true; break; }
        if(bUnsafe) continue;
        const int32 Distance=GridRules::Distance(Preferred,C);
        if(Distance<Best) { Best=Distance; Result=C; }
    }
    return Best!=MAX_int32;
}

void AGridPawn::RespawnPlayer(FIntPoint Cell)
{
    ResetWizardState();
    CurrentCell=Cell; Destination=Cell; FootHeight=0; FallSpeed=0; BurnFraction=0;
    Health=GridRules::MaxHealth; RespawnRemaining=0;
    bMoving=false; bJumping=false; MoveTime=0;
    JumpBufferRemaining=0.f; CoyoteRemaining=0.f;
    TripRemaining=0.f; KnockbackVelocity=FVector::ZeroVector;
    PendingMoveInput=FIntPoint::ZeroValue; bWaitingForMoveChord=false; MoveChordAge=0;
    SelectedSkill=INDEX_NONE; bHasAim=false; bValidAim=false; AimEnemy=INDEX_NONE;
    CastingSkill=INDEX_NONE; SpellCastAnimation=0.f;
    if(CastingHands) {
        CastingHands->SetVisibility(true,true);
        GridArt::AnimateCastingHands(CastingHands,INDEX_NONE,INDEX_NONE,0,0,0,0);
    }
    for(float& Cooldown:Cooldowns) Cooldown=0;
    SetActorLocation(GridRules::Center(Cell,GridRules::ActorOriginHeight));
    PlayerWorldPosition=GetActorLocation();
    // 给重生的玩家留出一整个攻击间隔来反应。
    EnemyAttackSpacing=0.f;
    for(auto& T:Targets)
    {
        T.AttackCooldown=GridRules::SlashCooldown; T.Windup=0.f; T.SlashRemaining=0.f;
        T.SkillRecoveryRemaining=0.f; T.AnimationStrikeNumber=0; T.ReleasedStrikeNumber=0;
        T.bSeesPlayer=false; T.SenseTime=0.f; T.StaggerRemaining=0.f;
        T.bCharging=false; T.ChargeRemaining=0.f; T.bWalking=false; T.TacticalShiftRemaining=0.f;
        T.StrikesRemaining=0; T.bSurgeFollowup=false; T.Skill=EWarriorSkill::Slash;
        T.bHasObservation=false; T.ObservedPlayerVelocity=FVector::ZeroVector;
        T.TacticRemaining=0.f; T.ThinkTime=0.f;
    }
    Feedback=TEXT("已重生：生命值 100｜Q / E / R / F 选择法术");
}

bool AGridPawn::EnemyCellOccupied(FIntPoint Cell,int32 Self) const
{
    // 查询某格是否被「其他」敌人占据（自身不算），直接查每帧重建的占位表。
    const int32* Who=OccupiedBy.Find(Cell);
    return Who && *Who!=Self;
}

void AGridPawn::RebuildOccupancy()
{
    // 每个战斗帧开头重建 格->敌人索引 快照，让后续占位查询是 O(1)。
    OccupiedBy.Reset();
    for(int32 I=0;I<Targets.Num();++I)
    {
        const auto& T=Targets[I];
        if(T.Health<=0 || !T.Actor.IsValid()) continue;
        OccupiedBy.Add(T.Cell,I);
        if(T.bWalking) OccupiedBy.Add(T.MoveDestination,I);
    }
}

bool AGridPawn::EnemyNextStep(int32 Index,bool bAllowWalls,FIntPoint& Next,const FIntPoint* GoalOverride,
    FIntPoint* ReachableGoal,bool bAllowPartial) const
{
    // 用 Dijkstra 求敌人到目标格的下一步；bAllowWalls 允许把拆墙也算进路径代价。
    const auto& T=Targets[Index];
    Next=T.Cell; // 无路可走时保持原位，绝不使用未初始化的下一格。
    const FIntPoint Goal=GoalOverride?*GoalOverride:(bLevelMode?T.LastKnownPlayer:GridRules::Cell(GetActorLocation()));
    if(ReachableGoal) *ReachableGoal=T.Cell;
    if(T.Cell==Goal) return false;
    auto& Open=PathScratch.Open; Open.Reset(); Open.Reserve(256);
    auto& Parents=PathScratch.Parents; Parents.Reset(); Parents.Reserve(256);
    auto& Costs=PathScratch.Costs; Costs.Reset(); Costs.Reserve(256);
    auto& Closed=PathScratch.Closed; Closed.Reset(); Closed.Reserve(256);
    Open.Add(T.Cell); Parents.Add(T.Cell,T.Cell); Costs.Add(T.Cell,0.f);
    FIntPoint Closest=T.Cell;
    auto GoalDistance=[Goal](FIntPoint C) { return (GridRules::Center(C)-GridRules::Center(Goal)).SizeSquared2D(); };
    auto FinishPath=[&](FIntPoint End)
    {
        if(ReachableGoal) *ReachableGoal=End;
        if(End==T.Cell) return false;
        Next=End;
        while(Parents[Next]!=T.Cell) Next=Parents[Next];
        return true;
    };
    const FIntPoint Directions[]={FIntPoint(1,0),FIntPoint(-1,0),FIntPoint(0,1),FIntPoint(0,-1),
        FIntPoint(1,1),FIntPoint(1,-1),FIntPoint(-1,1),FIntPoint(-1,-1)};
    while(!Open.IsEmpty())
    {
        int32 Best=0;
        for(int32 N=1;N<Open.Num();++N) if(Costs[Open[N]]<Costs[Open[Best]]) Best=N;
        const FIntPoint From=Open[Best]; Open.RemoveAtSwap(Best);
        if(Closed.Contains(From)) continue;
        Closed.Add(From);
        if(GoalDistance(From)<GoalDistance(Closest)) Closest=From;
        if(From==Goal)
        {
            return FinishPath(From);
        }
        for(const FIntPoint Direction:Directions)
        {
            const FIntPoint To=From+Direction;
            // 越界、已闭合、被敌人占据、被关卡阻挡、或撞上玩家正在走的格都跳过。
            if(!GridRules::Inside(To) || Closed.Contains(To)) continue;
            if(bLevelMode && LevelBlocked(To)) continue;
            if(bMoving && To==Destination && To!=Goal) continue;
            const float FromHeight=From==T.Cell?T.FootHeight:SurfaceHeight(From);
            const bool bDiagonal=Direction.X!=0 && Direction.Y!=0;
            if(bDiagonal)
            {
                const FIntPoint SideX=From+FIntPoint(Direction.X,0),SideY=From+FIntPoint(0,Direction.Y);
                const float MaxHeight=FromHeight+(bLevelMode?GridRules::LevelStepHeight:GridRules::MaxStepHeight);
                if((bLevelMode && (LevelBlocked(SideX) || LevelBlocked(SideY)))
                    || SurfaceHeight(SideX)>MaxHeight || SurfaceHeight(SideY)>MaxHeight) continue;
            }
            // 高差超过可迈高度时需要拆墙；把「要劈几下」折算成额外时间代价。
            const bool bNeedsDemolition=SurfaceHeight(To)>FromHeight+(bLevelMode?GridRules::LevelStepHeight:GridRules::MaxStepHeight);
            float Cost=(bDiagonal?1.41421356f:1.f)*GridRules::CellSize/GridRules::WarriorSpeed
                /MovementSpeedMultiplier(To,SurfaceHeight(To));
            if(bNeedsDemolition)
            {
                if(bDiagonal) continue; // 拆墙必须从墙面接近，不能从角落穿入。
                if(!bAllowWalls) continue;
                const FWall* Wall=Walls.FindByPredicate([To](const FWall& W){return W.Cell==To;});
                if(!Wall) continue;
                Cost+=FMath::CeilToFloat(float(Wall->Durability)/GridRules::SlashDemolition)*(GridRules::SlashCooldown+.45f);
            }
            if(From==T.Cell && !bNeedsDemolition
                && !WarriorCanTraverse(Index,T.Actor->GetActorLocation(),GridRules::Center(To,T.FootHeight+GridRules::ActorOriginHeight),false)) continue;
            // 关卡中避开燃烧地面：残血时更不愿穿火。
            if(bLevelMode) for(const auto& B:BurningCells)
                if(B.Cell==To && B.Remaining>0 && SurfaceHeight(To)<GridRules::GroundTolerance) Cost+=T.Health<=30?30.f:8.f;
            for(const auto& E:ElectricCells) if(E.Cell==To && E.Remaining>0 && IsConductiveCell(To)) Cost+=8.f;
            const float Candidate=Costs[From]+Cost;
            if(!Costs.Contains(To) || Candidate<Costs[To])
            { Costs.Add(To,Candidate); Parents.Add(To,From); Open.Add(To); }
        }
    }
    return bAllowPartial && FinishPath(Closest);
}

bool AGridPawn::TryWarriorSlash(int32 Index,int32 WallIndex)
{
    // 结算一次劈砍：要么砍玩家、要么拆土柱，需满足距离、高差与视线三条件。
    if(!Targets.IsValidIndex(Index) || Health<=0) return false;
    auto& T=Targets[Index];
    if(T.Health<=0 || !T.Actor.IsValid() || T.AttackCooldown>0.f || T.HoldRemaining>0.f) return false;
    const bool bWall=WallIndex!=INDEX_NONE;
    if(bWall && !Walls.IsValidIndex(WallIndex)) return false;
    const FVector Origin=T.Actor->GetActorLocation();
    const FVector End=bWall?GridRules::Center(Walls[WallIndex].Cell,T.FootHeight+GridRules::ActorOriginHeight):GetActorLocation();
    if(FVector::Dist2D(Origin,End)>GridRules::CellSize+GridRules::MeleeReachExtra) return false;
    if(bWall)
    {
        // 只有高到迈不上去的土柱才需要拆。
        if(SurfaceHeight(Walls[WallIndex].Cell)<=T.FootHeight+GridRules::MaxStepHeight) return false;
    }
    else if(FMath::Abs(T.FootHeight-FootHeight)>GridRules::MeleeReachHeight) return false;
    // 视线被地形挡住就不出手。
    FCollisionQueryParams Params; Params.AddIgnoredActor(T.Actor.Get()); Params.AddIgnoredActor(this);
    if(bWall) Params.AddIgnoredActor(Walls[WallIndex].Actor.Get());
    FHitResult Hit;
    if(GetWorld()->LineTraceSingleByChannel(Hit,Origin,End,ECC_Visibility,Params)) return false;
    T.Actor->SetActorRotation(FRotator(0,(End-Origin).Rotation().Yaw,0));
    T.AttackCooldown=GridRules::SlashCooldown; T.SlashRemaining=.25f;
    if(bWall) DamageWall(WallIndex,GridRules::SlashDemolition);
    else return QueueWarriorHit(Index);
    return true;
}

void AGridPawn::TickCombatants(float DeltaSeconds)
{
    if(bLevelMode) { TickLevelCombatants(DeltaSeconds); return; }
    const float DT=FMath::Max(0.f,DeltaSeconds);
    bool bAnyAlive=false;
    for(const auto& T:Targets) if(T.Health>0 && T.Actor.IsValid()) { bAnyAlive=true; break; }
    if(!bAnyAlive)
    {
        FIntPoint Safe;
        if(FindSpawnCell(EnemySpawnCell,false,Safe))
        {
            for(auto& T:Targets) if(T.Actor.IsValid()) T.Actor->Destroy();
            Targets.Reset(); SpawnWarrior(Safe);
        }
    }
    if(Health<=0)
    {
        RespawnRemaining=FMath::Max(0.f,RespawnRemaining-DT);
        FIntPoint Safe;
        if(RespawnRemaining<=0.f && FindSpawnCell(PlayerSpawnCell,true,Safe)) RespawnPlayer(Safe);
        return;
    }
    if(!bAnyAlive) return;
    EnemyAttackSpacing=FMath::Max(0.f,EnemyAttackSpacing-DT);
    RebuildOccupancy();
    for(int32 I=0;I<Targets.Num() && Health>0;++I) TickWarriorBrain(I,DT);
}
