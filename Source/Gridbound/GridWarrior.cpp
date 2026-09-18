#include "GridGame.h"
#include "GridArt.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"

// 敌人 AI：生成、重生选点、寻路（Dijkstra）、占位查询、劈砍结算与每帧状态机推进。

bool AGridPawn::SpawnWarrior(FIntPoint Cell)
{
    // 在指定格生成一名持剑敌人（冒险者模型 + 独立剑体）。
    if(!GridRules::Inside(Cell)) return false;
    FTarget T; T.Cell=Cell; T.HomeCell=Cell; T.MoveDestination=Cell; T.FootHeight=SurfaceHeight(Cell);
    T.Actor=MakeBlock(GridRules::Center(Cell,T.FootHeight+GridRules::ActorOriginHeight),FVector(.65f,.65f,1.5f),FLinearColor(.9f,.12f,.07f));
    if(!T.Actor.IsValid()) return false;
    Cast<UStaticMeshComponent>(T.Actor->GetRootComponent())->SetVisibility(false);
    auto* Visual=GridArt::CreateAdventurer(T.Actor.Get(),T.Actor->GetRootComponent(),TerrainMaterial?TerrainMaterial.Get():BaseMaterial.Get(),true);
    Visual->SetAbsolute(false,false,true); Visual->SetWorldScale3D(FVector::OneVector);
    auto* Sword=NewObject<UStaticMeshComponent>(T.Actor.Get());
    T.Actor->AddInstanceComponent(Sword); Sword->SetupAttachment(T.Actor->GetRootComponent());
    Sword->SetStaticMesh(CubeMesh); Sword->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Sword->SetAbsolute(false,false,true); Sword->SetWorldScale3D(FVector(.07f,.045f,.8f));
    Sword->SetRelativeLocation(FVector(35,40,12)); Sword->SetRelativeRotation(FRotator(-25,0,0));
    auto* Material=UMaterialInstanceDynamic::Create(BaseMaterial,T.Actor.Get());
    Material->SetVectorParameterValue(TEXT("Color"),FLinearColor(.7f,.8f,.9f));
    Sword->SetMaterial(0,Material); Sword->RegisterComponent(); T.Sword=Sword;
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
    CurrentCell=Cell; Destination=Cell; FootHeight=0; FallSpeed=0; BurnFraction=0;
    Health=GridRules::MaxHealth; RespawnRemaining=0;
    bMoving=false; bJumping=false; MoveTime=0;
    PendingMoveInput=FIntPoint::ZeroValue; bWaitingForMoveChord=false; MoveChordAge=0;
    SelectedSkill=INDEX_NONE; bHasAim=false; bValidAim=false; AimEnemy=INDEX_NONE;
    for(float& Cooldown:Cooldowns) Cooldown=0;
    SetActorLocation(GridRules::Center(Cell,GridRules::ActorOriginHeight));
    // 给重生的玩家留出一整个攻击间隔来反应。
    for(auto& T:Targets) T.AttackCooldown=GridRules::SlashCooldown;
    Feedback=TEXT("已重生：生命值 100｜按 Q、E、R 选择法术");
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

bool AGridPawn::EnemyNextStep(int32 Index,bool bAllowWalls,FIntPoint& Next,const FIntPoint* GoalOverride) const
{
    // 用 Dijkstra 求敌人到目标格的下一步；bAllowWalls 允许把拆墙也算进路径代价。
    const auto& T=Targets[Index];
    const FIntPoint Goal=GoalOverride?*GoalOverride:(bLevelMode?T.LastKnownPlayer:GridRules::Cell(GetActorLocation()));
    if(T.Cell==Goal) return false;
    auto& Open=PathScratch.Open; Open.Reset(); Open.Reserve(256);
    auto& Parents=PathScratch.Parents; Parents.Reset(); Parents.Reserve(256);
    auto& Costs=PathScratch.Costs; Costs.Reset(); Costs.Reserve(256);
    auto& Closed=PathScratch.Closed; Closed.Reset(); Closed.Reserve(256);
    Open.Add(T.Cell); Parents.Add(T.Cell,T.Cell); Costs.Add(T.Cell,0.f);
    const FIntPoint Directions[]={FIntPoint(1,0),FIntPoint(-1,0),FIntPoint(0,1),FIntPoint(0,-1)};
    while(!Open.IsEmpty())
    {
        int32 Best=0;
        for(int32 N=1;N<Open.Num();++N) if(Costs[Open[N]]<Costs[Open[Best]]) Best=N;
        const FIntPoint From=Open[Best]; Open.RemoveAtSwap(Best);
        if(Closed.Contains(From)) continue;
        Closed.Add(From);
        if(From==Goal)
        {
            // 回溯父链，把第一步（而非终点）作为返回值交给移动逻辑。
            Next=From;
            while(Parents[Next]!=T.Cell) Next=Parents[Next];
            return true;
        }
        for(const FIntPoint Direction:Directions)
        {
            const FIntPoint To=From+Direction;
            // 越界、已闭合、被敌人占据、被关卡阻挡、或撞上玩家正在走的格都跳过。
            if(!GridRules::Inside(To) || Closed.Contains(To) || EnemyCellOccupied(To,Index)) continue;
            if(bLevelMode && LevelBlocked(To)) continue;
            if(bMoving && To==Destination && To!=Goal) continue;
            const float FromHeight=From==T.Cell?T.FootHeight:SurfaceHeight(From);
            // 高差超过可迈高度时需要拆墙；把「要劈几下」折算成额外时间代价。
            const bool bNeedsDemolition=SurfaceHeight(To)>FromHeight+(bLevelMode?GridRules::LevelStepHeight:GridRules::MaxStepHeight);
            float Cost=GridRules::WarriorStepSeconds/MovementSpeedMultiplier(To,SurfaceHeight(To));
            if(bNeedsDemolition)
            {
                if(!bAllowWalls) continue;
                const FWall* Wall=Walls.FindByPredicate([To](const FWall& W){return W.Cell==To;});
                if(!Wall) continue;
                Cost+=FMath::CeilToFloat(float(Wall->Durability)/GridRules::SlashDemolition)*(GridRules::SlashCooldown+.45f);
            }
            // 关卡中避开燃烧地面：残血时更不愿穿火。
            if(bLevelMode) for(const auto& B:BurningCells)
                if(B.Cell==To && B.Remaining>0 && SurfaceHeight(To)<GridRules::GroundTolerance) Cost+=T.Health<=30?30.f:8.f;
            const float Candidate=Costs[From]+Cost;
            if(!Costs.Contains(To) || Candidate<Costs[To])
            { Costs.Add(To,Candidate); Parents.Add(To,From); Open.Add(To); }
        }
    }
    return false;
}

bool AGridPawn::TryWarriorSlash(int32 Index,int32 WallIndex)
{
    // 结算一次劈砍：要么砍玩家、要么拆土柱，需满足距离、高差与视线三条件。
    if(!Targets.IsValidIndex(Index) || Health<=0) return false;
    auto& T=Targets[Index];
    if(T.Health<=0 || !T.Actor.IsValid() || T.bWalking || T.AttackCooldown>0.f) return false;
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
    else TakeDamage(GridRules::SlashDamage,FDamageEvent(),nullptr,T.Actor.Get());
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
    RebuildOccupancy();
    for(int32 I=0;I<Targets.Num();++I)
    {
        auto& T=Targets[I];
        if(T.Health<=0 || !T.Actor.IsValid()) continue;
        T.AttackCooldown=FMath::Max(0.f,T.AttackCooldown-DT);
        T.SlashRemaining=FMath::Max(0.f,T.SlashRemaining-DT);
        T.ThinkTime=FMath::Max(0.f,T.ThinkTime-DT);
        if(T.Windup>0.f)
        {
            // 已锁定的攻击先预警，让玩家来得及离开被标记的格。
            T.Windup=FMath::Max(0.f,T.Windup-DT);
            DrawCell(T.StrikeCell,FColor::Orange);
            if(T.Windup<=0.f)
            {
                int32 WallIndex=INDEX_NONE;
                if(T.bStrikeWall) for(int32 W=0;W<Walls.Num();++W) if(Walls[W].Cell==T.StrikeCell) { WallIndex=W; break; }
                if((T.bStrikeWall && WallIndex!=INDEX_NONE) || (!T.bStrikeWall && GridRules::Cell(GetActorLocation())==T.StrikeCell))
                    TryWarriorSlash(I,WallIndex);
                T.AttackCooldown=GridRules::SlashCooldown;
            }
        }
        else if(T.bWalking)
        {
            const bool bBlocked=SurfaceHeight(T.MoveDestination)>T.FootHeight+GridRules::MaxStepHeight
                || EnemyCellOccupied(T.MoveDestination,I)
                || T.MoveDestination==GridRules::Cell(GetActorLocation())
                || (bMoving && T.MoveDestination==Destination);
            if(bBlocked)
            {
                T.bWalking=false; T.MoveProgress=0;
                T.Actor->SetActorLocation(GridRules::Center(T.Cell,T.FootHeight+GridRules::ActorOriginHeight));
            }
            else
            {
                T.MoveProgress+=DT*MovementSpeedMultiplier(GridRules::Cell(T.Actor->GetActorLocation()),T.FootHeight);
                const float Alpha=FMath::Clamp(T.MoveProgress/GridRules::WarriorStepSeconds,0.f,1.f);
                T.Actor->SetActorLocation(FMath::Lerp(GridRules::Center(T.Cell,T.FootHeight+GridRules::ActorOriginHeight),GridRules::Center(T.MoveDestination,T.FootHeight+GridRules::ActorOriginHeight),Alpha));
                if(Alpha>=1.f) { T.Cell=T.MoveDestination; T.bWalking=false; }
            }
        }
        else if(T.ThinkTime<=0.f)
        {
            // 按思考节拍做决策；攻击经预警延迟生效，而不是瞬间命中。
            T.ThinkTime=.2f+I*.025f;
            FIntPoint Next;
            const FIntPoint PlayerCell=GridRules::Cell(GetActorLocation());
            bool bAttack=false,bWall=false;
            if(EnemyNextStep(I,false,Next) || EnemyNextStep(I,true,Next))
            {
                if(SurfaceHeight(Next)>T.FootHeight+GridRules::MaxStepHeight)
                {
                    bAttack=true; bWall=true;
                }
                else if(FVector::Dist2D(T.Actor->GetActorLocation(),GetActorLocation())<=GridRules::CellSize+GridRules::MeleeReachExtra
                    && FMath::Abs(T.FootHeight-FootHeight)<=GridRules::MeleeReachHeight)
                {
                    Next=PlayerCell; bAttack=true;
                }
            }
            if(bAttack && T.AttackCooldown<=0.f)
            {
                T.StrikeCell=Next; T.bStrikeWall=bWall; T.Windup=GridRules::MeleeWindup;
                T.Actor->SetActorRotation(FRotator(0,(GridRules::Center(Next)-T.Actor->GetActorLocation()).Rotation().Yaw,0));
            }
            else if(!bAttack && Next!=PlayerCell && !(bMoving && Next==Destination))
            {
                T.MoveDestination=Next; T.MoveProgress=0; T.bWalking=true;
                T.Actor->SetActorRotation(FRotator(0,FVector(Next.X-T.Cell.X,Next.Y-T.Cell.Y,0).Rotation().Yaw,0));
            }
        }
        if(T.Sword.IsValid())
            T.Sword->SetRelativeRotation(FRotator(T.Windup>0.f?-65.f:T.SlashRemaining>0.f?FMath::Lerp(65.f,-70.f,T.SlashRemaining/.25f):-25.f,0,0));
        if(Health<=0) break;
    }
}
