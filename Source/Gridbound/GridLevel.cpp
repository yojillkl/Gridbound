#include "GridGame.h"
#include "GridArt.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

// 单人决斗：保留可交互地形，击败唯一的战斗大师即通关。
bool AGridPawn::LevelBlocked(FIntPoint C) const
{
    return !GridRules::Inside(C) || GridArt::CliffAt(C)
        || GridArt::TerrainAt(C)==GridArt::ETerrain::River;
}

float AGridPawn::LevelHeight(FIntPoint C) const
{
    if(GridArt::PlatformAt(C)) return 300.f;
    if(C.Y>=15 && C.Y<=17 && C.X>=22 && C.X<=25) return (C.X-21)*60.f;
    return 0.f;
}

void AGridPawn::BuildLevel()
{
    for(auto& A:LevelProps) if(A.IsValid()) A->Destroy();
    LevelProps.Reset();
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
}

void AGridPawn::ResetLevel()
{
    bLevelComplete=false;
    LevelSeconds=0; LevelDeaths=0; for(int32& Count:LevelCasts) Count=0;
    PlayerSpawnCell=FIntPoint(2,10); RespawnPlayer(PlayerSpawnCell);
    if(auto* PC=Cast<APlayerController>(GetController())) PC->SetControlRotation(FRotator(-8,0,0));
    BuildLevel(); StartEncounter();
    Feedback=TEXT("法术已就绪");
}

void AGridPawn::StartEncounter()
{
    bEncounterStarted=false;
    for(auto& T:Targets) if(T.Actor.IsValid()) T.Actor->Destroy();
    Targets.Reset();
    SpawnWarrior(FIntPoint(7,10));
}

FString AGridPawn::LevelObjective() const
{
    if(bLevelComplete) return TEXT("战斗大师已被击败，通关！按 F5 再战。");
    if(Health<=0) return TEXT("决斗失败，2 秒后重新挑战；战士也会恢复。");
    return TEXT("目标：击败唯一的战斗大师。向前进入战场，或用火球开战。");
}

void AGridPawn::InteractLevel()
{
    // 决斗没有拾取或撤离交互，击败敌人自动结算。
}

void AGridPawn::TickLevel(float DT)
{
    if(bLevelComplete) return;
    LevelSeconds+=FMath::Max(0.f,DT);
    // 未成功生成敌人、玩家倒下或同时阵亡都不误判为胜利。
    if(Health>0 && Targets.Num()==1 && Targets[0].Health<=0)
    {
        bLevelComplete=true;
        Feedback=TEXT("胜利！战斗大师已被击败。");
        UE_LOG(LogTemp,Display,TEXT("GRIDBOUND_LEVEL_COMPLETE duel seconds=%.1f deaths=%d"),LevelSeconds,LevelDeaths);
    }
}
bool AGridPawn::EnemySeesPlayer(const FTarget& T) const
{
    // 入口是「安全阅读区」：开火或踏入营地才会真正触发战斗。
    if(bLevelMode && !bEncounterStarted && GridRules::Cell(GetActorLocation()).X<4 && T.AlertTime<=0.f) return false;
    const float Range=T.AlertTime>0.f?GridRules::EnemyTrackingRange:GridRules::EnemySightRange;
    if(!T.Actor.IsValid() || Health<=0 || FVector::DistSquared2D(T.Actor->GetActorLocation(),GetActorLocation())>FMath::Square(Range)) return false;
    FCollisionQueryParams Params; Params.AddIgnoredActor(this);
    for(const auto& Other:Targets) if(Other.Actor.IsValid()) Params.AddIgnoredActor(Other.Actor.Get());
    FHitResult Hit;
    const FVector Eye=T.Actor->GetActorLocation()+FVector(0,0,45);
    for(const float Height:{55.f,0.f,-35.f})
        if(!GetWorld()->LineTraceSingleByChannel(Hit,Eye,GetActorLocation()+FVector(0,0,Height),ECC_Visibility,Params)) return true;
    return false;
}

void AGridPawn::TickLevelCombatants(float DeltaSeconds)
{
    const float DT=FMath::Max(0.f,DeltaSeconds);
    EnemyAttackSpacing=FMath::Max(0.f,EnemyAttackSpacing-DT);
    if(bLevelComplete) return;
    if(Health>0 && GridRules::Cell(GetActorLocation()).X>=4) bEncounterStarted=true;
    if(Health<=0)
    {
        RespawnRemaining=FMath::Max(0.f,RespawnRemaining-DT);
        if(RespawnRemaining<=0)
        {
            const int32 Deaths=LevelDeaths;
            ResetArena(); LevelDeaths=Deaths;
        }
        return;
    }
    RebuildOccupancy();
    for(int32 I=0;I<Targets.Num() && Health>0;++I) TickWarriorBrain(I,DT);
}
