#include "GridGame.h"
#include "GridArt.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

namespace
{
    const FIntPoint Beacons[]={FIntPoint(5,10),FIntPoint(12,10),FIntPoint(18,10)};
    const FIntPoint Checkpoints[]={FIntPoint(2,10),FIntPoint(8,10),FIntPoint(14,10)};
}

bool AGridPawn::LevelBlocked(FIntPoint C) const
{
    if(!GridRules::Inside(C) || GridArt::CliffAt(C)) return true;
    if(GridArt::TerrainAt(C)==GridArt::ETerrain::River) return true;
    if(C.X==7 && LevelStage<1) return true;
    if(C.X==13 && LevelStage<2) return true;
    for(const FIntPoint Beacon:Beacons) if(C==Beacon) return true;
    return false;
}

float AGridPawn::LevelHeight(FIntPoint C) const
{
    return GridArt::PlatformAt(C)?300.f:0.f;
}

void AGridPawn::BuildLevel()
{
    for(auto& A:LevelProps) if(A.IsValid()) A->Destroy();
    LevelProps.Reset(); LevelGates.Reset(); LevelBeacons.Reset();
    auto Stone=[this](FIntPoint C,float Height)
    {
        AActor* A=MakeBlock(GridRules::Center(C,Height*.5f),FVector(1.5f,1.5f,Height/100.f),FLinearColor(.2f,.3f,.35f));
        Cast<UStaticMeshComponent>(A->GetRootComponent())->SetVisibility(false);
        auto* Mesh=GridArt::CreateStone(A,A->GetRootComponent(),TerrainMaterial?TerrainMaterial.Get():BaseMaterial.Get(),Height);
        Mesh->SetAbsolute(false,false,true); Mesh->SetWorldScale3D(FVector::OneVector);
        LevelProps.Add(A); return A;
    };
    for(int32 X=0;X<20;++X) for(int32 Y=0;Y<20;++Y)
    {
        const FIntPoint C(X,Y);
        if(GridArt::CliffAt(C)) Stone(C,650.f);
        else if(GridArt::PlatformAt(C)) Stone(C,300.f);
    }
    for(int32 X:{7,13}) for(int32 Y=9;Y<=11;++Y)
    {
        auto* A=Stone(FIntPoint(X,Y),600.f); A->Tags.Add(TEXT("LevelGate")); LevelGates.Add(A);
    }
    for(int32 I=0;I<3;++I)
    {
        const float Height=LevelHeight(Beacons[I]);
        AActor* A=MakeBlock(GridRules::Center(Beacons[I],Height+60),FVector(.5f,.5f,1.6f),FLinearColor(1,.7f,.1f));
        Cast<UStaticMeshComponent>(A->GetRootComponent())->SetVisibility(false);
        auto* Mesh=GridArt::CreateBeacon(A,A->GetRootComponent(),TerrainMaterial?TerrainMaterial.Get():BaseMaterial.Get(),
            I==0?FLinearColor(2,.25f,.03f):I==1?FLinearColor(.1f,.65f,2.f):FLinearColor(.4f,1.5f,.3f));
        Mesh->SetAbsolute(false,false,true); Mesh->SetWorldScale3D(FVector::OneVector);
        LevelProps.Add(A); LevelBeacons.Add(A);
    }
}

void AGridPawn::ResetLevel()
{
    LevelStage=0; bLevelComplete=false; bFireSeal=false; bRainSeal=false;
    LevelSeconds=0; LevelDeaths=0; for(int32& Count:LevelCasts) Count=0;
    PlayerSpawnCell=Checkpoints[0]; RespawnPlayer(PlayerSpawnCell);
    BuildLevel(); StartEncounter();
    Feedback=TEXT("Camp: Q + LMB the orange beacon. Defeat guards, then F nearby.");
    if(auto* PC=Cast<APlayerController>(GetController())) PC->SetControlRotation(FRotator(-8,0,0));
}

void AGridPawn::StartEncounter()
{
    for(auto& T:Targets) if(T.Actor.IsValid()) T.Actor->Destroy();
    Targets.Reset();
    const TArray<FIntPoint> Cells=LevelStage==0?TArray<FIntPoint>{FIntPoint(5,6),FIntPoint(5,14)}:
        LevelStage==1?TArray<FIntPoint>{FIntPoint(9,7),FIntPoint(12,8),FIntPoint(12,13)}:
        TArray<FIntPoint>{FIntPoint(15,6),FIntPoint(17,7),FIntPoint(15,14),FIntPoint(18,13)};
    for(const auto C:Cells) SpawnWarrior(C);
}

FString AGridPawn::LevelObjective() const
{
    int32 Alive=0; for(const auto& T:Targets) if(T.Health>0 && T.Actor.IsValid()) ++Alive;
    if(bLevelComplete) return TEXT("All three beacons secured. Level cleared! F5 to play again.");
    if(LevelStage==0) return FString::Printf(TEXT("1/3 CAMP | Fireball the orange beacon [%s] | Guards %d | F to secure"),bFireSeal?TEXT("DONE"):TEXT("Q"),Alive);
    if(LevelStage==1) return FString::Printf(TEXT("2/3 CROSSING | Rain on blue beacon [%s] | Guards %d | F to secure"),bRainSeal?TEXT("DONE"):TEXT("R"),Alive);
    return FString::Printf(TEXT("3/3 SUMMIT | Guards %d | E lift beside the platform, step onto it, F at green beacon"),Alive);
}

bool AGridPawn::LevelFireImpact(AActor* Actor)
{
    for(int32 I=0;I<LevelBeacons.Num();++I) if(LevelBeacons[I].Get()==Actor)
    {
        if(I==0 && LevelStage==0) { bFireSeal=true; Feedback=TEXT("Camp beacon lit. Defeat guards, then F beside the beacon."); }
        return true;
    }
    return false;
}

void AGridPawn::InteractLevel()
{
    if(bLevelComplete || Health<=0) return;
    const FIntPoint Goal=Beacons[LevelStage];
    if(FVector::Dist2D(GetActorLocation(),GridRules::Center(Goal))>GridRules::CellSize*1.15f
        || FMath::Abs(FootHeight-LevelHeight(Goal))>50.f)
    { Feedback=TEXT("Move beside the active beacon, at the same height, then press F."); return; }
    for(const auto& T:Targets) if(T.Health>0 && T.Actor.IsValid())
    { Feedback=TEXT("Defeat the guards in this area first."); return; }
    if(LevelStage==0 && !bFireSeal) { Feedback=TEXT("Light this beacon with Q Fireball first."); return; }
    if(LevelStage==1 && !bRainSeal) { Feedback=TEXT("Cool this beacon with R Rain first."); return; }
    if(LevelStage==2)
    {
        bLevelComplete=true; Feedback=TEXT("ELEMENTAL CROSSING COMPLETE | F5 restart");
        UE_LOG(LogTemp,Display,TEXT("GRIDBOUND_LEVEL_COMPLETE seconds=%.1f deaths=%d"),LevelSeconds,LevelDeaths);
        return;
    }
    const int32 GateX=LevelStage==0?7:13;
    for(auto& Gate:LevelGates) if(Gate.IsValid() && GridRules::Cell(Gate->GetActorLocation()).X==GateX) Gate->Destroy();
    ++LevelStage;
    PlayerSpawnCell=Checkpoints[LevelStage]; RespawnPlayer(PlayerSpawnCell); StartEncounter();
    Feedback=LevelStage==1?TEXT("Checkpoint secured. R Rain clears the burning crossing and slows guards."):
        TEXT("Final checkpoint. Split guards with E. Raise yourself beside the high platform.");
}

void AGridPawn::TickLevel(float DT)
{
    if(!bLevelComplete) LevelSeconds+=FMath::Max(0.f,DT);
    if(LevelStage==1 && !bRainSeal)
        for(int32 X=9;X<=11;++X) for(int32 Y=9;Y<=11;++Y)
        {
            bool bBurning=false;
            for(const auto& B:BurningCells) if(B.Cell==FIntPoint(X,Y) && B.Remaining>1.f) bBurning=true;
            if(!bBurning) IgniteCell(FIntPoint(X,Y));
        }
    if(LevelBeacons.IsValidIndex(LevelStage) && LevelBeacons[LevelStage].IsValid())
    {
        const FVector P=GridRules::Center(Beacons[LevelStage],LevelHeight(Beacons[LevelStage])+180);
        DrawDebugLine(GetWorld(),P,P+FVector(0,0,200),FColor::Yellow,false,0,0,5);
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
        if(EnemySeesPlayer(T)) T.AlertTime=8.f;
        if(T.AlertTime>7.9f) for(auto& Other:Targets)
            if(Other.Health>0 && GridRules::Distance(Other.Cell,T.Cell)<=3) Other.AlertTime=FMath::Max(Other.AlertTime,4.f);
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
            if(LevelBlocked(T.MoveDestination) || SurfaceHeight(T.MoveDestination)>T.FootHeight+20
                || EnemyCellOccupied(T.MoveDestination,I) || T.MoveDestination==GridRules::Cell(GetActorLocation())
                || (bMoving && T.MoveDestination==Destination))
            {
                T.bWalking=false; T.Actor->SetActorLocation(GridRules::Center(T.Cell,T.FootHeight+75));
            }
            else
            {
                T.MoveProgress+=DT*MovementSpeedMultiplier(GridRules::Cell(T.Actor->GetActorLocation()),T.FootHeight);
                const float Alpha=FMath::Clamp(T.MoveProgress/GridRules::WarriorStepSeconds,0.f,1.f);
                T.Actor->SetActorLocation(FMath::Lerp(GridRules::Center(T.Cell,T.FootHeight+75),GridRules::Center(T.MoveDestination,T.FootHeight+75),Alpha));
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
            if(T.FootHeight<3 && IsBurning(T.Cell))
            {
                float Best=MAX_flt;
                for(const FIntPoint D:{FIntPoint(1,0),FIntPoint(-1,0),FIntPoint(0,1),FIntPoint(0,-1)})
                {
                    const FIntPoint C=T.Cell+D;
                    if(LevelBlocked(C) || IsBurning(C) || EnemyCellOccupied(C,I) || C==PlayerCell
                        || (bMoving && C==Destination) || SurfaceHeight(C)>T.FootHeight+20) continue;
                    const float Score=GridRules::Distance(C,PlayerCell)*(T.Health<=30?-1.f:1.f);
                    if(Score<Best) { Best=Score; Next=C; bEscaping=true; }
                }
            }
            if(!bEscaping && T.AlertTime>0)
            {
                if(FVector::Dist2D(T.Actor->GetActorLocation(),GetActorLocation())<=GridRules::CellSize+10 && FMath::Abs(T.FootHeight-FootHeight)<=100)
                { Next=PlayerCell; bAttack=true; }
                else if(EnemyNextStep(I,true,Next) && SurfaceHeight(Next)>T.FootHeight+20) { bAttack=true; bWall=true; }
            }
            else if(!bEscaping)
            {
                // Short local patrol: no global knowledge of the player's position.
                const FIntPoint Patrol=T.HomeCell+FIntPoint(0,(int32(LevelSeconds/3)+I)%2?1:-1);
                Next=T.Cell==Patrol?T.HomeCell:Patrol;
                if(GridRules::Distance(T.Cell,Next)>1) Next=T.Cell+GridRules::Cardinal(GridRules::Center(T.HomeCell-T.Cell));
            }
            if(bAttack && T.AttackCooldown<=0)
            {
                T.StrikeCell=Next; T.bStrikeWall=bWall; T.Windup=.45f;
                T.Actor->SetActorRotation(FRotator(0,(GridRules::Center(Next)-T.Actor->GetActorLocation()).Rotation().Yaw,0));
            }
            else if(!bAttack && Next!=T.Cell && Next!=PlayerCell && !(bMoving && Next==Destination)
                && !LevelBlocked(Next) && !EnemyCellOccupied(Next,I) && SurfaceHeight(Next)<=T.FootHeight+20)
            {
                T.MoveDestination=Next; T.MoveProgress=0; T.bWalking=true;
                T.Actor->SetActorRotation(FRotator(0,GridRules::Center(Next-T.Cell).Rotation().Yaw,0));
            }
        }
        if(T.Sword.IsValid()) T.Sword->SetRelativeRotation(FRotator(T.Windup>0?-65:T.SlashRemaining>0?65:-25,0,0));
        if(Health<=0) break;
    }
}
