#include "GridGame.h"
#include "GridArt.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

bool AGridPawn::LevelBlocked(FIntPoint C) const
{
    return !GridRules::Inside(C) || GridArt::CliffAt(C)
        || GridArt::TerrainAt(C)==GridArt::ETerrain::River
        || (!bRelicCarried && C==RelicCell);
}

float AGridPawn::LevelHeight(FIntPoint C) const
{
    if(GridArt::PlatformAt(C)) return 300.f;
    // Five 60 cm steps: jumping is an alternative to raising an earth wall.
    if(C.Y>=15 && C.Y<=17 && C.X>=22 && C.X<=25) return (C.X-21)*60.f;
    return 0.f;
}

void AGridPawn::BuildLevel()
{
    for(auto& A:LevelProps) if(A.IsValid()) A->Destroy();
    LevelProps.Reset(); LevelBeacons.Reset();
    for(int32 X=0;X<GridRules::BoardSize;++X) for(int32 Y=0;Y<GridRules::BoardSize;++Y)
    {
        const FIntPoint C(X,Y);
        const float Height=GridArt::CliffAt(C)?(X==0 || Y==0 || X==31 || Y==31?650.f:480.f):LevelHeight(C);
        if(Height<=0) continue;
        AActor* A=MakeBlock(GridRules::Center(C,Height*.5f),FVector(1.5f,1.5f,Height/100.f),FLinearColor(.2f,.3f,.35f));
        Cast<UStaticMeshComponent>(A->GetRootComponent())->SetVisibility(false);
        auto* Mesh=GridArt::CreateStone(A,A->GetRootComponent(),TerrainMaterial?TerrainMaterial.Get():BaseMaterial.Get(),Height);
        Mesh->SetAbsolute(false,false,true); Mesh->SetWorldScale3D(FVector::OneVector);
        LevelProps.Add(A);
    }
    for(const FIntPoint C:{RelicCell,PlayerSpawnCell})
    {
        const bool bRelic=C==RelicCell;
        // Keep the camp marker behind the arrival point, clear of the first-person camera.
        const FIntPoint MarkerCell=bRelic?C:C+FIntPoint(-1,0);
        AActor* A=MakeBlock(GridRules::Center(MarkerCell,LevelHeight(C)+60),FVector(.5f,.5f,1.6f),FLinearColor(1,.7f,.1f),bRelic);
        Cast<UStaticMeshComponent>(A->GetRootComponent())->SetVisibility(false);
        auto* Mesh=GridArt::CreateBeacon(A,A->GetRootComponent(),TerrainMaterial?TerrainMaterial.Get():BaseMaterial.Get(),
            bRelic?FLinearColor(2,.8f,.05f):FLinearColor(.05f,1.5f,.8f));
        Mesh->SetAbsolute(false,false,true); Mesh->SetWorldScale3D(FVector::OneVector);
        LevelProps.Add(A); LevelBeacons.Add(A);
    }
}

void AGridPawn::ResetLevel()
{
    LevelStage=0; bLevelComplete=false; bRelicCarried=false; RelicCell=FIntPoint(27,16);
    LevelSeconds=0; LevelDeaths=0; for(int32& Count:LevelCasts) Count=0;
    PlayerSpawnCell=FIntPoint(2,10); RespawnPlayer(PlayerSpawnCell);
    BuildLevel(); StartEncounter();
    Feedback=TEXT("穿过河道，取回金色晶核，再返回绿色营地。");
}

void AGridPawn::StartEncounter()
{
    for(auto& T:Targets) if(T.Actor.IsValid()) T.Actor->Destroy();
    Targets.Reset();
    for(const FIntPoint C:{FIntPoint(7,8),FIntPoint(8,13),FIntPoint(13,19),FIntPoint(15,23),
        FIntPoint(17,10),FIntPoint(18,14),FIntPoint(23,13),FIntPoint(24,19),FIntPoint(28,15),FIntPoint(29,18)})
    {
        if(SpawnWarrior(C))
        {
            auto& T=Targets.Last(); T.FootHeight=LevelHeight(C);
            T.Actor->SetActorLocation(GridRules::Center(C,T.FootHeight+GridRules::ActorOriginHeight));
        }
    }
}

FString AGridPawn::LevelObjective() const
{
    if(bLevelComplete) return TEXT("晶核已带回营地，通关！按 F5 重新挑战。");
    if(Health<=0) return TEXT("你已倒下，即将在营地重生；晶核掉落在原处。");
    if(bRelicCarried) return TEXT("携带晶核返回绿色营地，靠近后按 F 撤离；无需消灭所有守卫。");
    return TEXT("前往金色光柱，跳上遗迹阶梯或用土墙登高；靠近晶核按 F 拾取。");
}

bool AGridPawn::LevelFireImpact(AActor* Actor)
{
    for(const auto& A:LevelBeacons) if(A.Get()==Actor) return true;
    return false;
}

void AGridPawn::InteractLevel()
{
    if(bLevelComplete || Health<=0) return;
    const FIntPoint Goal=bRelicCarried?PlayerSpawnCell:RelicCell;
    if(FVector::Dist2D(GetActorLocation(),GridRules::Center(Goal))>GridRules::CellSize*1.5f
        || FMath::Abs(FootHeight-SurfaceHeight(Goal))>65.f) return;
    if(bRelicCarried)
    {
        bLevelComplete=true;
        UE_LOG(LogTemp,Display,TEXT("GRIDBOUND_LEVEL_COMPLETE seconds=%.1f deaths=%d"),LevelSeconds,LevelDeaths);
        return;
    }
    bRelicCarried=true; LevelStage=1;
    if(LevelBeacons[0].IsValid()) { LevelBeacons[0]->SetActorHiddenInGame(true); LevelBeacons[0]->SetActorEnableCollision(false); }
    // Local alarm draws nearby guards, while distant patrols keep their own information.
    for(auto& T:Targets) if(T.Health>0 && GridRules::Distance(T.Cell,RelicCell)<=12)
    { T.AlertTime=12.f; T.LastKnownPlayer=CurrentCell; }
}

void AGridPawn::TickLevel(float DT)
{
    if(!bLevelComplete) LevelSeconds+=FMath::Max(0.f,DT);
    if(Health<=0 && bRelicCarried)
    {
        bRelicCarried=false; LevelStage=0;
        RelicCell=GridRules::Cell(GetActorLocation());
        if(LevelBeacons[0].IsValid())
        {
            LevelBeacons[0]->SetActorLocation(GridRules::Center(RelicCell,LevelHeight(RelicCell)+60));
            LevelBeacons[0]->SetActorHiddenInGame(false);
            LevelBeacons[0]->SetActorEnableCollision(true);
        }
    }
    if(!bLevelComplete)
    {
        const FIntPoint Goal=bRelicCarried?PlayerSpawnCell:RelicCell;
        if(!bRelicCarried && LevelBeacons[0].IsValid())
            LevelBeacons[0]->SetActorLocation(GridRules::Center(RelicCell,SurfaceHeight(RelicCell)+60));
        const FVector P=GridRules::Center(Goal,SurfaceHeight(Goal)+180);
        DrawDebugLine(GetWorld(),P,P+FVector(0,0,1500),bRelicCarried?FColor::Green:FColor::Yellow,false,0,0,8);
    }
}

bool AGridPawn::EnemySeesPlayer(const FTarget& T) const
{
    // The entrance is a reading space; shooting or entering the camp starts the fight.
    if(LevelStage==0 && GridRules::Cell(GetActorLocation()).X<4 && T.AlertTime<=0.f) return false;
    if(!T.Actor.IsValid() || Health<=0 || FVector::Dist2D(T.Actor->GetActorLocation(),GetActorLocation())>5.f*GridRules::CellSize) return false;
    FCollisionQueryParams Params; Params.AddIgnoredActor(this);
    for(const auto& Other:Targets) if(Other.Actor.IsValid()) Params.AddIgnoredActor(Other.Actor.Get());
    FHitResult Hit;
    return !GetWorld()->LineTraceSingleByChannel(Hit,T.Actor->GetActorLocation()+FVector(0,0,45),SpellOrigin(),ECC_Visibility,Params);
}

void AGridPawn::TickLevelCombatants(float DeltaSeconds)
{
    const float DT=FMath::Max(0.f,DeltaSeconds);
    if(bLevelComplete) return;
    if(Health<=0)
    {
        RespawnRemaining=FMath::Max(0.f,RespawnRemaining-DT);
        FIntPoint Safe;
        if(RespawnRemaining<=0 && FindSpawnCell(PlayerSpawnCell,true,Safe)) RespawnPlayer(Safe);
        return;
    }
    for(int32 I=0;I<Targets.Num();++I)
    {
        auto& T=Targets[I]; if(T.Health<=0 || !T.Actor.IsValid()) continue;
        T.AttackCooldown=FMath::Max(0.f,T.AttackCooldown-DT);
        T.ThinkTime=FMath::Max(0.f,T.ThinkTime-DT);
        T.SlashRemaining=FMath::Max(0.f,T.SlashRemaining-DT);
        T.AlertTime=FMath::Max(0.f,T.AlertTime-DT);
        if(EnemySeesPlayer(T)) { T.AlertTime=8.f; T.LastKnownPlayer=GridRules::Cell(GetActorLocation()); }
        if(T.AlertTime>7.9f) for(auto& Other:Targets)
            if(Other.Health>0 && GridRules::Distance(Other.Cell,T.Cell)<=3) { Other.AlertTime=FMath::Max(Other.AlertTime,4.f); Other.LastKnownPlayer=T.LastKnownPlayer; }
        if(T.Windup>0.f)
        {
            T.Windup=FMath::Max(0.f,T.Windup-DT);
            DrawCell(T.StrikeCell,FColor::Orange);
            if(T.Windup<=0.f)
            {
                int32 WallIndex=INDEX_NONE;
                if(T.bStrikeWall) for(int32 W=0;W<Walls.Num();++W) if(Walls[W].Cell==T.StrikeCell) { WallIndex=W; break; }
                if((T.bStrikeWall && WallIndex!=INDEX_NONE) || (!T.bStrikeWall && GridRules::Cell(GetActorLocation())==T.StrikeCell)) TryWarriorSlash(I,WallIndex);
                T.AttackCooldown=GridRules::SlashCooldown;
            }
        }
        else if(T.bWalking)
        {
            if(LevelBlocked(T.MoveDestination) || SurfaceHeight(T.MoveDestination)>T.FootHeight+GridRules::LevelStepHeight
                || EnemyCellOccupied(T.MoveDestination,I) || T.MoveDestination==GridRules::Cell(GetActorLocation())
                || (bMoving && T.MoveDestination==Destination))
            {
                T.bWalking=false; T.Actor->SetActorLocation(GridRules::Center(T.Cell,T.FootHeight+GridRules::ActorOriginHeight));
            }
            else
            {
                T.MoveProgress+=DT*MovementSpeedMultiplier(GridRules::Cell(T.Actor->GetActorLocation()),T.FootHeight);
                const float Alpha=FMath::Clamp(T.MoveProgress/GridRules::WarriorStepSeconds,0.f,1.f);
                T.Actor->SetActorLocation(FMath::Lerp(GridRules::Center(T.Cell,T.FootHeight+GridRules::ActorOriginHeight),GridRules::Center(T.MoveDestination,T.FootHeight+GridRules::ActorOriginHeight),Alpha));
                if(Alpha>=1) { T.Cell=T.MoveDestination; T.bWalking=false; }
            }
        }
        else if(T.ThinkTime<=0.f)
        {
            T.ThinkTime=.2f+I*.025f;
            FIntPoint Next=T.Cell;
            bool bAttack=false,bWall=false;
            const FIntPoint PlayerCell=GridRules::Cell(GetActorLocation());
            auto IsBurning=[this](FIntPoint C)
            { for(const auto& B:BurningCells) if(B.Cell==C && B.Remaining>0) return true; return false; };
            bool bEscaping=false;
            if(T.FootHeight<GridRules::GroundTolerance && IsBurning(T.Cell))
            {
                float Best=MAX_flt;
                for(const FIntPoint D:{FIntPoint(1,0),FIntPoint(-1,0),FIntPoint(0,1),FIntPoint(0,-1)})
                {
                    const FIntPoint C=T.Cell+D;
                    if(LevelBlocked(C) || IsBurning(C) || EnemyCellOccupied(C,I) || C==PlayerCell
                        || (bMoving && C==Destination) || SurfaceHeight(C)>T.FootHeight+GridRules::LevelStepHeight) continue;
                    const float Score=GridRules::Distance(C,PlayerCell)*(T.Health<=30?-1.f:1.f);
                    if(Score<Best) { Best=Score; Next=C; bEscaping=true; }
                }
            }
            if(!bEscaping && T.AlertTime>0)
            {
                if(FVector::Dist2D(T.Actor->GetActorLocation(),GetActorLocation())<=GridRules::CellSize+10 && FMath::Abs(T.FootHeight-FootHeight)<=100)
                { Next=PlayerCell; bAttack=true; }
                else if(EnemyNextStep(I,true,Next) && SurfaceHeight(Next)>T.FootHeight+GridRules::LevelStepHeight) { bAttack=true; bWall=true; }
            }
            else if(!bEscaping)
            {
                // Short local patrol: no global knowledge of the player's position.
                FIntPoint Patrol=T.HomeCell+FIntPoint(0,(int32(LevelSeconds/3)+I)%2?1:-1);
                if(LevelBlocked(Patrol) || LevelHeight(Patrol)!=LevelHeight(T.HomeCell) || T.Cell==Patrol) Patrol=T.HomeCell;
                EnemyNextStep(I,false,Next,&Patrol);
            }
            if(bAttack && T.AttackCooldown<=0)
            {
                T.StrikeCell=Next; T.bStrikeWall=bWall; T.Windup=.45f;
                T.Actor->SetActorRotation(FRotator(0,(GridRules::Center(Next)-T.Actor->GetActorLocation()).Rotation().Yaw,0));
            }
            else if(!bAttack && Next!=T.Cell && Next!=PlayerCell && !(bMoving && Next==Destination)
                && !LevelBlocked(Next) && !EnemyCellOccupied(Next,I) && SurfaceHeight(Next)<=T.FootHeight+GridRules::LevelStepHeight)
            {
                T.MoveDestination=Next; T.MoveProgress=0; T.bWalking=true;
                T.Actor->SetActorRotation(FRotator(0,GridRules::Center(Next-T.Cell).Rotation().Yaw,0));
            }
        }
        if(T.Sword.IsValid()) T.Sword->SetRelativeRotation(FRotator(T.Windup>0?-65:T.SlashRemaining>0?65:-25,0,0));
        if(Health<=0) break;
    }
}
