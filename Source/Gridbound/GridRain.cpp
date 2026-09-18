#include "GridGame.h"
#include "GridArt.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/PlayerController.h"

// 降雨系统：地形显隐、泥地减速、土墙可放置判定、降雨施法与湿地形/雨水动画。

void AGridPawn::SetTerrainVisible(FIntPoint Cell,bool bVisible)
{
    // 切换某个地形格的可见性（焦土/泥地会盖掉原来的草地）。
    if(const auto* Tile=TerrainTiles.Find(Cell);Tile && Tile->IsValid())
        if(auto* Mesh=Cast<UPrimitiveComponent>((*Tile)->GetRootComponent())) Mesh->SetVisibility(bVisible);
}

float AGridPawn::MovementSpeedMultiplier(FIntPoint Cell,float FeetHeight) const
{
    // 泥地上贴地移动减速一半；脚离地（在土柱/跳跃中）不受影响。
    if(FeetHeight>GridRules::GroundTolerance) return 1.f;
    for(const auto& Mud:MuddyCells) if(Mud.Cell==Cell && Mud.Remaining>0.f) return .5f;
    return 1.f;
}

TArray<FIntPoint> AGridPawn::PlaceableWallCells(FIntPoint CenterCell) const
{
    // 计算某中心格下土墙实际能放的格：排除越界、超距、被占据、视线被挡的格。
    TArray<FIntPoint> Result;
    if(!GridRules::Inside(CenterCell) || GridRules::Distance(CurrentCell,CenterCell)>GridRules::WallRange) return Result;
    FCollisionQueryParams Params; Params.AddIgnoredActor(this);
    // 角色会被土墙托起来，所以它们不是建造时的障碍。
    for(const auto& Target:Targets) if(Target.Actor.IsValid()) Params.AddIgnoredActor(Target.Actor.Get());
    for(const FIntPoint Cell:GridRules::WallCells(CenterCell,bWallAlongX))
    {
        if(!GridRules::Inside(Cell) || GridRules::Distance(CurrentCell,Cell)>GridRules::WallRange) continue;
        if(bLevelMode && (LevelBlocked(Cell) || LevelHeight(Cell)>0.f)) continue;
        bool bOccupied=false;
        for(const auto& Wall:Walls) if(Wall.Actor.IsValid() && Wall.Cell==Cell) { bOccupied=true; break; }
        if(bOccupied) continue;
        TArray<FOverlapResult> Overlaps;
        GetWorld()->OverlapMultiByChannel(Overlaps,GridRules::Center(Cell,GridRules::WallHeight*.5f+1),FQuat::Identity,
            ECC_Visibility,FCollisionShape::MakeBox(FVector(74.5f,74.5f,GridRules::WallHeight*.5f-1)),Params);
        for(const auto& Overlap:Overlaps)
            if(Overlap.bBlockingHit && Overlap.GetActor() && !Overlap.GetActor()->ActorHasTag(TEXT("GridTerrain"))) { bOccupied=true; break; }
        if(bOccupied) continue;
        FHitResult Hit;
        if(GetWorld()->LineTraceSingleByChannel(Hit,SpellOrigin(),GridRules::Center(Cell,5),ECC_Visibility,Params)) continue;
        Result.Add(Cell);
    }
    return Result;
}

bool AGridPawn::CanCastRain(FIntPoint CenterCell) const
{
    // 降雨能否施放：在射程内，且中心格（含其上的土柱顶）有清晰视线。
    if(!GridRules::Inside(CenterCell) || GridRules::Distance(CurrentCell,CenterCell)>GridRules::RainRange) return false;
    const AActor* Occupant=nullptr;
    for(const auto& Target:Targets) if(Target.Health>0 && Target.Cell==CenterCell) Occupant=Target.Actor.Get();
    // 瞄准土柱时，以它可见的顶部作为降雨的目标点。
    for(const auto& Wall:Walls) if(Wall.Actor.IsValid() && Wall.Cell==CenterCell) Occupant=Wall.Actor.Get();
    return HasLineOfSight(CenterCell,Occupant);
}

void AGridPawn::MakeMud(FIntPoint Cell)
{
    // 把某格变成泥地：先扑灭火焰，再生成泥地外观并盖住原草地。
    if(!GridRules::Inside(Cell) || GridArt::TerrainAt(Cell)==GridArt::ETerrain::River) return;
    for(int32 I=BurningCells.Num()-1;I>=0;--I) if(BurningCells[I].Cell==Cell)
    {
        if(BurningCells[I].Actor.IsValid()) BurningCells[I].Actor->Destroy();
        BurningCells.RemoveAtSwap(I);
    }
    for(auto& Mud:MuddyCells) if(Mud.Cell==Cell) { Mud.Remaining=GridRules::TerrainRecovery; return; }
    FMuddyCell Mud; Mud.Cell=Cell;
    Mud.Actor=GridArt::CreateMud(GetWorld(),Cell,TerrainMaterial?TerrainMaterial.Get():BaseMaterial.Get());
    MuddyCells.Add(Mud); SetTerrainVisible(Cell,false);
}

bool AGridPawn::CastRain(FIntPoint CenterCell)
{
    // 施放降雨：覆盖范围内的焦土、砂石与已有泥地全部变成泥地。
    if(!CanCastRain(CenterCell)) return false;
    for(const FIntPoint Cell:GridRules::RainCells(CenterCell))
    {
        if(!GridRules::Inside(Cell)) continue;
        bool bBurnt=false,bMuddy=false;
        for(const auto& Burning:BurningCells) if(Burning.Cell==Cell) { bBurnt=true; break; }
        for(const auto& Mud:MuddyCells) if(Mud.Cell==Cell) { bMuddy=true; break; }
        if(bBurnt || bMuddy || GridArt::TerrainAt(Cell)==GridArt::ETerrain::Gravel) MakeMud(Cell);
    }
    FRainEffect Rain;
    Rain.Actor=GridArt::CreateRain(GetWorld(),CenterCell,TerrainMaterial?TerrainMaterial.Get():BaseMaterial.Get());
    RainEffects.Add(Rain);
    return true;
}

void AGridPawn::TickWetTerrain(float DeltaSeconds)
{
    // 湿地形计时：泥地到期恢复草地，雨水动画到期销毁。
    const float DT=FMath::Max(0.f,DeltaSeconds);
    for(int32 I=MuddyCells.Num()-1;I>=0;--I)
    {
        auto& Mud=MuddyCells[I]; Mud.Remaining-=DT;
        if(Mud.Remaining<=0.f)
        {
            if(Mud.Actor.IsValid()) Mud.Actor->Destroy();
            SetTerrainVisible(Mud.Cell,true); MuddyCells.RemoveAtSwap(I);
        }
    }
    for(int32 I=RainEffects.Num()-1;I>=0;--I)
    {
        auto& Rain=RainEffects[I]; Rain.Age+=DT;
        if(Rain.Age>=GridRules::RainVisualLifetime || !Rain.Actor.IsValid())
        {
            if(Rain.Actor.IsValid()) Rain.Actor->Destroy();
            RainEffects.RemoveAtSwap(I);
        }
        else GridArt::AnimateRain(Rain.Actor.Get(),Rain.Age);
    }
}

void AGridPawn::SetupRainShowcase()
{
#if !UE_BUILD_SHIPPING
    CurrentCell=FIntPoint(2,7); Destination=CurrentCell;
    SetActorLocation(GridRules::Center(CurrentCell,GridRules::ActorOriginHeight));
    IgniteCell(FIntPoint(4,6)); IgniteCell(FIntPoint(5,7)); IgniteCell(FIntPoint(4,8));
    CastRain(FIntPoint(5,8));
    // 只让这场演示的雨水保持可见，直到延后的截图拍完。
    if(RainEffects.Num()) RainEffects[0].Age=-5.f;
    if(auto* PC=Cast<APlayerController>(GetController())) PC->SetControlRotation(FRotator(-6,12,0));
    SelectedSkill=2; Feedback=TEXT("降雨：熄灭草地火焰，砂石与焦土变为泥地，移动减速一半");
#endif
}
