#include "GridGame.h"
#include "GridArt.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"

bool AGridPawn::SpawnWarrior(FIntPoint Cell)
{
    if(!GridRules::Inside(Cell)) return false;
    FTarget T; T.Cell=Cell; T.HomeCell=Cell; T.MoveDestination=Cell; T.FootHeight=SurfaceHeight(Cell);
    T.Actor=MakeBlock(GridRules::Center(Cell,T.FootHeight+75),FVector(.65f,.65f,1.5f),FLinearColor(.9f,.12f,.07f));
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
    SetActorLocation(GridRules::Center(Cell,75));
    // Give the returning player a full attack interval to react.
    for(auto& T:Targets) T.AttackCooldown=GridRules::SlashCooldown;
    Feedback=TEXT("Respawned: 100 HP | Q / E / R to select a spell");
}

bool AGridPawn::EnemyCellOccupied(FIntPoint Cell,int32 Self) const
{
    for(int32 I=0;I<Targets.Num();++I) if(I!=Self)
    {
        const auto& T=Targets[I];
        if(T.Health>0 && T.Actor.IsValid() && (T.Cell==Cell || (T.bWalking && T.MoveDestination==Cell))) return true;
    }
    return false;
}

bool AGridPawn::EnemyNextStep(int32 Index,bool bAllowWalls,FIntPoint& Next) const
{
    const auto& T=Targets[Index];
    const FIntPoint Goal=GridRules::Cell(GetActorLocation());
    if(T.Cell==Goal) return false;
    TArray<FIntPoint> Open; Open.Add(T.Cell);
    TMap<FIntPoint,FIntPoint> Parents; Parents.Add(T.Cell,T.Cell);
    TMap<FIntPoint,float> Costs; Costs.Add(T.Cell,0.f);
    TSet<FIntPoint> Closed;
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
            Next=From;
            while(Parents[Next]!=T.Cell) Next=Parents[Next];
            return true;
        }
        for(const FIntPoint Direction:Directions)
        {
            const FIntPoint To=From+Direction;
            if(!GridRules::Inside(To) || Closed.Contains(To) || EnemyCellOccupied(To,Index)) continue;
            if(bLevelMode && LevelBlocked(To)) continue;
            if(bMoving && To==Destination && To!=Goal) continue;
            const float FromHeight=From==T.Cell?T.FootHeight:SurfaceHeight(From);
            const bool bNeedsDemolition=SurfaceHeight(To)>FromHeight+20.f;
            float Cost=GridRules::WarriorStepSeconds/MovementSpeedMultiplier(To,SurfaceHeight(To));
            if(bNeedsDemolition)
            {
                if(!bAllowWalls) continue;
                const FWall* Wall=Walls.FindByPredicate([To](const FWall& W){return W.Cell==To;});
                if(!Wall) continue;
                Cost+=FMath::CeilToFloat(float(Wall->Durability)/GridRules::SlashDemolition)*(GridRules::SlashCooldown+.45f);
            }
            if(bLevelMode) for(const auto& B:BurningCells)
                if(B.Cell==To && B.Remaining>0 && SurfaceHeight(To)<3) Cost+=T.Health<=30?30.f:8.f;
            const float Candidate=Costs[From]+Cost;
            if(!Costs.Contains(To) || Candidate<Costs[To])
            { Costs.Add(To,Candidate); Parents.Add(To,From); Open.Add(To); }
        }
    }
    return false;
}

bool AGridPawn::TryWarriorSlash(int32 Index,int32 WallIndex)
{
    if(!Targets.IsValidIndex(Index) || Health<=0) return false;
    auto& T=Targets[Index];
    if(T.Health<=0 || !T.Actor.IsValid() || T.bWalking || T.AttackCooldown>0.f) return false;
    const bool bWall=WallIndex!=INDEX_NONE;
    if(bWall && !Walls.IsValidIndex(WallIndex)) return false;
    const FVector Origin=T.Actor->GetActorLocation();
    const FVector End=bWall?GridRules::Center(Walls[WallIndex].Cell,T.FootHeight+75):GetActorLocation();
    if(FVector::Dist2D(Origin,End)>GridRules::CellSize+10.f) return false;
    if(bWall)
    {
        if(SurfaceHeight(Walls[WallIndex].Cell)<=T.FootHeight+20.f) return false;
    }
    else if(FMath::Abs(T.FootHeight-FootHeight)>100.f) return false;
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
    for(int32 I=0;I<Targets.Num();++I)
    {
        auto& T=Targets[I];
        if(T.Health<=0 || !T.Actor.IsValid()) continue;
        T.AttackCooldown=FMath::Max(0.f,T.AttackCooldown-DT);
        T.SlashRemaining=FMath::Max(0.f,T.SlashRemaining-DT);
        if(T.bWalking)
        {
            const bool bBlocked=SurfaceHeight(T.MoveDestination)>T.FootHeight+20.f
                || EnemyCellOccupied(T.MoveDestination,I)
                || T.MoveDestination==GridRules::Cell(GetActorLocation())
                || (bMoving && T.MoveDestination==Destination);
            if(bBlocked)
            {
                T.bWalking=false; T.MoveProgress=0;
                T.Actor->SetActorLocation(GridRules::Center(T.Cell,T.FootHeight+75));
            }
            else
            {
                T.MoveProgress+=DT*MovementSpeedMultiplier(GridRules::Cell(T.Actor->GetActorLocation()),T.FootHeight);
                const float Alpha=FMath::Clamp(T.MoveProgress/GridRules::WarriorStepSeconds,0.f,1.f);
                T.Actor->SetActorLocation(FMath::Lerp(GridRules::Center(T.Cell,T.FootHeight+75),GridRules::Center(T.MoveDestination,T.FootHeight+75),Alpha));
                if(Alpha>=1.f) { T.Cell=T.MoveDestination; T.bWalking=false; }
            }
        }
        if(!T.bWalking && !TryWarriorSlash(I))
        {
            FIntPoint Next;
            if(EnemyNextStep(I,false,Next) || EnemyNextStep(I,true,Next))
            {
                if(SurfaceHeight(Next)>T.FootHeight+20.f)
                {
                    for(int32 W=0;W<Walls.Num();++W) if(Walls[W].Cell==Next) { TryWarriorSlash(I,W); break; }
                }
                else if(Next!=GridRules::Cell(GetActorLocation()) && !(bMoving && Next==Destination))
                {
                    T.MoveDestination=Next; T.MoveProgress=0; T.bWalking=true;
                    T.Actor->SetActorRotation(FRotator(0,FVector(Next.X-T.Cell.X,Next.Y-T.Cell.Y,0).Rotation().Yaw,0));
                }
            }
        }
        if(T.Sword.IsValid())
            T.Sword->SetRelativeRotation(FRotator(T.SlashRemaining>0.f?FMath::Lerp(65.f,-70.f,T.SlashRemaining/.25f):-25.f,0,0));
        if(Health<=0) break;
    }
}
