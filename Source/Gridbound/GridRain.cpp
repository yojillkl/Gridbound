#include "GridGame.h"
#include "GridArt.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/PlayerController.h"

void AGridPawn::SetTerrainVisible(FIntPoint Cell,bool bVisible)
{
    if(const auto* Tile=TerrainTiles.Find(Cell);Tile && Tile->IsValid())
        if(auto* Mesh=Cast<UPrimitiveComponent>((*Tile)->GetRootComponent())) Mesh->SetVisibility(bVisible);
}

float AGridPawn::MovementSpeedMultiplier(FIntPoint Cell,float FeetHeight) const
{
    if(FeetHeight>3.f) return 1.f;
    for(const auto& Mud:MuddyCells) if(Mud.Cell==Cell && Mud.Remaining>0.f) return .5f;
    return 1.f;
}

TArray<FIntPoint> AGridPawn::PlaceableWallCells(FIntPoint CenterCell) const
{
    TArray<FIntPoint> Result;
    if(!GridRules::Inside(CenterCell) || GridRules::Distance(CurrentCell,CenterCell)>GridRules::WallRange) return Result;
    FCollisionQueryParams Params; Params.AddIgnoredActor(this);
    // Characters are lifted by the wall, so they are not construction obstacles.
    for(const auto& Target:Targets) if(Target.Actor.IsValid()) Params.AddIgnoredActor(Target.Actor.Get());
    for(const FIntPoint Cell:GridRules::WallCells(CenterCell,bWallAlongX))
    {
        if(!GridRules::Inside(Cell) || GridRules::Distance(CurrentCell,Cell)>GridRules::WallRange) continue;
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
    if(!GridRules::Inside(CenterCell) || GridRules::Distance(CurrentCell,CenterCell)>GridRules::RainRange) return false;
    const AActor* Occupant=nullptr;
    for(const auto& Target:Targets) if(Target.Health>0 && Target.Cell==CenterCell) Occupant=Target.Actor.Get();
    // Aiming at a pillar selects its visible top for rain targeting.
    for(const auto& Wall:Walls) if(Wall.Actor.IsValid() && Wall.Cell==CenterCell) Occupant=Wall.Actor.Get();
    return HasLineOfSight(CenterCell,Occupant);
}

void AGridPawn::MakeMud(FIntPoint Cell)
{
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
    SetActorLocation(GridRules::Center(CurrentCell,75));
    IgniteCell(FIntPoint(4,6)); IgniteCell(FIntPoint(5,7)); IgniteCell(FIntPoint(4,8));
    CastRain(FIntPoint(5,8));
    // Keep only the demonstration's rain visible until the delayed screenshot.
    if(RainEffects.Num()) RainEffects[0].Age=-5.f;
    if(auto* PC=Cast<APlayerController>(GetController())) PC->SetControlRotation(FRotator(-6,12,0));
    SelectedSkill=2; Feedback=TEXT("Rain / extinguished grass and gravel become mud / speed -50%");
#endif
}
