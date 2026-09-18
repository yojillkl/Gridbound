#include "GridGame.h"
#include "GridArt.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"

FVector AGridPawn::SpellOrigin() const
{
    return GetActorLocation()+FVector(0,0,GridRules::EyeHeight-75.f);
}

float AGridPawn::SurfaceHeight(FIntPoint Cell) const
{
    for(const auto& Wall:Walls) if(Wall.Cell==Cell && Wall.Actor.IsValid() && Wall.Durability>0)
        return GridRules::WallHeight*FMath::Clamp(Wall.Age/GridRules::WallRiseTime,.005f,1.f);
    return bLevelMode?LevelHeight(Cell):0.f;
}

void AGridPawn::UpdateElevation(float DeltaSeconds)
{
    const float DT=FMath::Max(0.f,DeltaSeconds);
    auto FollowSupport=[DT](float Support,float& Height,float& Speed)
    {
        if(Support>=Height) { Height=Support; Speed=0.f; }
        else
        {
            Height=FMath::Max(Support,Height-Speed*DT-.5f*GridRules::Gravity*DT*DT);
            Speed=Height<=Support?0.f:Speed+GridRules::Gravity*DT;
        }
    };
    // Physical XY determines support; logical cells continue to determine combat occupancy.
    const float Support=SurfaceHeight(GridRules::Cell(GetActorLocation()));
    if(Support>FootHeight+.01f)
    {
        // A newly rising pillar lifts the pawn even during a jump.
        FootHeight=Support; FallSpeed=0.f; bJumping=false;
    }
    else if(bJumping || FootHeight>Support+.01f)
    {
        const float Gravity=bJumping?GridRules::JumpGravity:GridRules::Gravity;
        const float NextHeight=FootHeight-FallSpeed*DT-.5f*Gravity*DT*DT;
        FallSpeed+=Gravity*DT;
        if(NextHeight<=Support && FallSpeed>=0.f) { FootHeight=Support; FallSpeed=0.f; bJumping=false; }
        else FootHeight=FMath::Max(Support,NextHeight);
    }
    else { FootHeight=Support; FallSpeed=0.f; bJumping=false; }
    FVector Location=GetActorLocation(); Location.Z=FootHeight+75; SetActorLocation(Location);
    for(auto& Target:Targets) if(Target.Health>0 && Target.Actor.IsValid())
    {
        FVector TargetLocation=Target.Actor->GetActorLocation();
        FollowSupport(SurfaceHeight(GridRules::Cell(TargetLocation)),Target.FootHeight,Target.FallSpeed);
        TargetLocation.Z=Target.FootHeight+75;
        Target.Actor->SetActorLocation(TargetLocation);
    }
}

void AGridPawn::TickWalls(float DeltaSeconds)
{
    const float DT=FMath::Max(0.f,DeltaSeconds);
    for(int32 I=Walls.Num()-1;I>=0;--I)
    {
        auto& Wall=Walls[I]; Wall.Remaining-=DT; Wall.Age+=DT;
        if(Wall.Remaining<=0.f || !Wall.Actor.IsValid()) { RemoveWall(I); continue; }
        const float Rise=FMath::Clamp(Wall.Age/GridRules::WallRiseTime,.005f,1.f);
        Wall.Actor->SetActorLocation(GridRules::Center(Wall.Cell,GridRules::WallHeight*.5f*Rise));
        Wall.Actor->SetActorScale3D(FVector(1.5f,1.5f,4.5f*Rise));
        if(Wall.Visual.IsValid()) Wall.Visual->SetWorldScale3D(FVector(1,1,Rise));
    }
}

void AGridPawn::RemoveWall(int32 Index)
{
    if(!Walls.IsValidIndex(Index)) return;
    auto& Wall=Walls[Index];
    GridArt::CreateRubble(GetWorld(),Wall.Cell,TerrainMaterial?TerrainMaterial.Get():BaseMaterial.Get());
    if(Wall.Actor.IsValid()) Wall.Actor->Destroy();
    Walls.RemoveAtSwap(Index);
}

void AGridPawn::DamageWall(int32 Index,int32 Demolition)
{
    if(!Walls.IsValidIndex(Index) || Demolition<=0) return;
    auto& Wall=Walls[Index];
    Wall.Durability=FMath::Max(0,Wall.Durability-Demolition);
    if(Wall.Durability==0) { RemoveWall(Index); return; }
    if(!Wall.Actor.IsValid()) return;
    if(Wall.Visual.IsValid()) Wall.Visual->DestroyComponent();
    Wall.Visual=GridArt::CreateEarthPillar(Wall.Actor.Get(),Wall.Actor->GetRootComponent(),
        TerrainMaterial?TerrainMaterial.Get():BaseMaterial.Get(),Wall.Cell,Wall.Durability);
    Wall.Visual->SetAbsolute(false,false,true);
    Wall.Visual->SetWorldScale3D(FVector(1,1,FMath::Clamp(Wall.Age/GridRules::WallRiseTime,.005f,1.f)));
}

void AGridPawn::ResolveFireballImpact(AActor* HitActor,FVector ImpactPoint)
{
    if(!HitActor) return;
    if(bLevelMode && LevelFireImpact(HitActor)) return;
    // Character damage and object demolition never share a health pool or application path.
    for(auto& Target:Targets) if(Target.Health>0 && Target.Actor.Get()==HitActor)
    {
        Target.Health=FMath::Max(0,Target.Health-GridRules::FireballDamage);
        Target.AlertTime=8.f; Target.LastKnownPlayer=GridRules::Cell(GetActorLocation());
        if(Target.Health==0) HitActor->Destroy();
        Feedback=TEXT("火球命中：造成 50 伤害");
        return;
    }
    if(auto* Character=Cast<AGridPawn>(HitActor))
    {
        Character->TakeDamage(GridRules::FireballDamage,FDamageEvent(),GetController(),this);
        return;
    }
    for(int32 I=0;I<Walls.Num();++I) if(Walls[I].Actor.Get()==HitActor)
    {
        DamageWall(I,GridRules::FireballDemolition);
        Feedback=TEXT("火球命中：造成 50 土柱拆毁值");
        return;
    }
    if(HitActor->ActorHasTag(TEXT("GridTerrain")))
    {
        // Prefer the hit tile's own cell so a seam impact cannot ignite a neighbouring river tile.
        IgniteCell(GridRules::Cell(HitActor->GetActorLocation()));
    }
}

bool AGridPawn::IgniteCell(FIntPoint Cell)
{
    if(!GridRules::Inside(Cell) || (bLevelMode && LevelBlocked(Cell)) || GridArt::TerrainAt(Cell)!=GridArt::ETerrain::Grass) return false;
    for(const auto& Mud:MuddyCells) if(Mud.Cell==Cell && Mud.Remaining>0.f) return false;
    for(auto& Burning:BurningCells) if(Burning.Cell==Cell)
    {
        Burning.Remaining=GridRules::BurnLifetime;
        Burning.RecoveryRemaining=GridRules::TerrainRecovery;
        if(Burning.Actor.IsValid())
        {
            TInlineComponentArray<UProceduralMeshComponent*> Meshes; Burning.Actor->GetComponents(Meshes);
            for(auto* Mesh:Meshes) if(Mesh->ComponentHasTag(TEXT("GridFlame"))) Mesh->SetVisibility(true);
        }
        return true;
    }
    FBurningCell Burning; Burning.Cell=Cell;
    Burning.Actor=GridArt::CreateBurningGrass(GetWorld(),Cell,TerrainMaterial?TerrainMaterial.Get():BaseMaterial.Get());
    BurningCells.Add(Burning);
    if(const auto* Tile=TerrainTiles.Find(Cell);Tile && Tile->IsValid())
        if(auto* Mesh=Cast<UPrimitiveComponent>((*Tile)->GetRootComponent())) Mesh->SetVisibility(false);
    Feedback=TEXT("草地已点燃：每秒 10 伤害，持续 10 秒");
    return true;
}

void AGridPawn::TickBurning(float DeltaSeconds)
{
    const float DT=FMath::Max(0.f,DeltaSeconds);
    for(int32 I=BurningCells.Num()-1;I>=0;--I)
    {
        auto& Burning=BurningCells[I];
        if(Burning.Remaining<=0.f)
        {
            Burning.RecoveryRemaining-=DT;
            if(Burning.RecoveryRemaining<=0.f)
            {
                if(Burning.Actor.IsValid()) Burning.Actor->Destroy();
                SetTerrainVisible(Burning.Cell,true); BurningCells.RemoveAtSwap(I);
            }
            continue;
        }
        const float ActiveTime=FMath::Min(FMath::Max(0.f,DeltaSeconds),Burning.Remaining);
        Burning.Remaining=FMath::Max(0.f,Burning.Remaining-ActiveTime); Burning.Age+=ActiveTime;
        auto Burn=[ActiveTime](float& Fraction)
        {
            Fraction+=ActiveTime*GridRules::BurnDamagePerSecond;
            const int32 Damage=FMath::FloorToInt(Fraction+.00001f);
            Fraction=FMath::Max(0.f,Fraction-Damage); return Damage;
        };
        // Raised characters are above the flames, not standing on the burning floor.
        if(Health>0 && CurrentCell==Burning.Cell && FootHeight<=3.f)
            TakeDamage(Burn(BurnFraction),FDamageEvent(),nullptr,nullptr);
        for(auto& Target:Targets) if(Target.Health>0 && Target.Actor.IsValid()
            && GridRules::Cell(Target.Actor->GetActorLocation())==Burning.Cell && Target.FootHeight<=3.f)
        {
            Target.Health=FMath::Max(0,Target.Health-Burn(Target.BurnFraction));
            if(Target.Health==0 && Target.Actor.IsValid()) Target.Actor->Destroy();
        }
        if(Burning.Actor.IsValid())
        {
            GridArt::AnimateBurningGrass(Burning.Actor.Get(),Burning.Age);
            if(Burning.Remaining<=0.f)
            {
                TInlineComponentArray<UProceduralMeshComponent*> Meshes; Burning.Actor->GetComponents(Meshes);
                for(auto* Mesh:Meshes) if(Mesh->ComponentHasTag(TEXT("GridFlame"))) Mesh->SetVisibility(false);
            }
        }
        if(Burning.Remaining<=0.f)
        {
            Burning.RecoveryRemaining-=FMath::Max(0.f,DT-ActiveTime);
            if(Burning.RecoveryRemaining<=0.f)
            {
                if(Burning.Actor.IsValid()) Burning.Actor->Destroy();
                SetTerrainVisible(Burning.Cell,true); BurningCells.RemoveAtSwap(I);
            }
        }
    }
}

void AGridPawn::SetupCombatShowcase()
{
#if !UE_BUILD_SHIPPING
    // Opt-in deterministic visual QA; ordinary play never creates this scene.
    CurrentCell=FIntPoint(1,4); Destination=CurrentCell;
    SetActorLocation(GridRules::Center(CurrentCell,75));
    bWallAlongX=false; PlaceWall(FIntPoint(6,4)); TickWalls(.4f); UpdateElevation(.4f);
    for(int32 I=0;I<Walls.Num();++I)
    {
        if(Walls[I].Cell==FIntPoint(6,3)) DamageWall(I,50);
        if(Walls[I].Cell==FIntPoint(6,5)) DamageWall(I,75);
    }
    IgniteCell(FIntPoint(3,4)); IgniteCell(FIntPoint(4,5));
    AActor* Display=MakeBlock(GridRules::Center(FIntPoint(3,3),180),FVector::OneVector,FLinearColor::White,false);
    Cast<UStaticMeshComponent>(Display->GetRootComponent())->SetVisibility(false);
    GridArt::CreateFireball(Display,Display->GetRootComponent(),TerrainMaterial);
    Display->SetActorRotation(FRotator(-15,30,0)); Display->Tags.Add(TEXT("GridSpellDebris"));
    if(auto* PC=Cast<APlayerController>(GetController())) PC->SetControlRotation(FRotator(4,0,0));
    SelectedSkill=0; Feedback=TEXT("效果展示：完整、受损、重损土柱与燃烧草地");
#endif
}
