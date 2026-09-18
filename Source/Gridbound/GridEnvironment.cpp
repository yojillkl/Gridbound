#include "GridGame.h"
#include "GridArt.h"
#include "ProceduralMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"

// 环境系统：施法原点、地表高度、角色升降、土柱生命周期、火球结算与燃烧/泥地。

FVector AGridPawn::SpellOrigin() const
{
    // 法术射线从眼睛高度出发。
    return GetActorLocation()+FVector(0,0,GridRules::EyeHeight-GridRules::ActorOriginHeight);
}

float AGridPawn::SurfaceHeight(FIntPoint Cell) const
{
    // 地表高度：有土柱时取它当前升起的高度，否则取关卡地形（竞技场为 0）。
    for(const auto& Wall:Walls) if(Wall.Cell==Cell && Wall.Actor.IsValid() && Wall.Durability>0)
        return GridRules::WallHeight*FMath::Clamp(Wall.Age/GridRules::WallRiseTime,.005f,1.f);
    return bLevelMode?LevelHeight(Cell):0.f;
}

void AGridPawn::UpdateElevation(float DeltaSeconds)
{
    // 每帧用「地表支撑 + 重力」驱动玩家和敌人的垂直位置；支撑高于脚底就直接托起。
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
    // 物理 XY 决定支撑面；逻辑格继续决定战斗占位。
    const float Support=SurfaceHeight(GridRules::Cell(GetActorLocation()));
    if(Support>FootHeight+.01f)
    {
        // 正在升起的土柱会把角色托起来，哪怕此刻正在跳跃中。
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
    FVector Location=GetActorLocation(); Location.Z=FootHeight+GridRules::ActorOriginHeight; SetActorLocation(Location);
    for(auto& Target:Targets) if(Target.Health>0 && Target.Actor.IsValid())
    {
        FVector TargetLocation=Target.Actor->GetActorLocation();
        FollowSupport(SurfaceHeight(GridRules::Cell(TargetLocation)),Target.FootHeight,Target.FallSpeed);
        TargetLocation.Z=Target.FootHeight+GridRules::ActorOriginHeight;
        Target.Actor->SetActorLocation(TargetLocation);
    }
}

void AGridPawn::TickWalls(float DeltaSeconds)
{
    // 推进每根土柱的计时：按升起曲线长高，到期则移除。
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
    // 移除一根土柱并留下一堆碎石装饰。
    if(!Walls.IsValidIndex(Index)) return;
    auto& Wall=Walls[Index];
    GridArt::CreateRubble(GetWorld(),Wall.Cell,TerrainMaterial?TerrainMaterial.Get():BaseMaterial.Get());
    if(Wall.Actor.IsValid()) Wall.Actor->Destroy();
    Walls.RemoveAtSwap(Index);
}

void AGridPawn::DamageWall(int32 Index,int32 Demolition)
{
    // 削减土柱耐久，耐久归零则移除；否则按剩余耐久重建它的外观。
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
    // 火球命中后的统一结算：依次判断角色、土柱、地形，按类型分流。
    if(!HitActor) return;
    if(bLevelMode && LevelFireImpact(HitActor)) return;
    // 角色伤害与物件拆毁从不共用生命池，也走不同的结算路径。
    for(auto& Target:Targets) if(Target.Health>0 && Target.Actor.Get()==HitActor)
    {
        Target.Health=FMath::Max(0,Target.Health-GridRules::FireballDamage);
        Target.AlertTime=8.f; Target.LastKnownPlayer=GridRules::Cell(GetActorLocation());
        if(Target.Health==0) HitActor->Destroy();
        Feedback=FString::Printf(TEXT("火球命中：造成 %d 伤害"),GridRules::FireballDamage);
        return;
    }
    // 目前不可达：火球无法命中施法者自己，也没有第二名玩家可打。作为防御性兜底保留。
    if(auto* Character=Cast<AGridPawn>(HitActor))
    {
        Character->TakeDamage(GridRules::FireballDamage,FDamageEvent(),GetController(),this);
        return;
    }
    for(int32 I=0;I<Walls.Num();++I) if(Walls[I].Actor.Get()==HitActor)
    {
        DamageWall(I,GridRules::FireballDemolition);
        Feedback=FString::Printf(TEXT("火球命中：造成 %d 土柱拆毁值"),GridRules::FireballDemolition);
        return;
    }
    if(HitActor->ActorHasTag(TEXT("GridTerrain")))
    {
        // 优先取命中地块自身的格，避免打在接缝处时误点燃相邻的河道格。
        IgniteCell(GridRules::Cell(HitActor->GetActorLocation()));
    }
}

bool AGridPawn::IgniteCell(FIntPoint Cell)
{
    // 点燃草地：只在草地、非泥地且未被关卡阻挡的格生效；已在燃烧则续期。
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
    // 燃烧系统：给站在火上的角色累计伤害、推进火焰动画，并在燃尽后恢复地表。
    const float DT=FMath::Max(0.f,DeltaSeconds);
    // 小数伤害只会在「正站在活火上」时累积；角色离开后残留的零头不能泄漏到下一次点燃。
    auto StillBurning=[this](FIntPoint Cell)
    {
        for(const auto& B:BurningCells) if(B.Cell==Cell && B.Remaining>0.f) return true;
        return false;
    };
    if(!StillBurning(CurrentCell)) BurnFraction=0.f;
    for(auto& Target:Targets) if(Target.Actor.IsValid() && !StillBurning(GridRules::Cell(Target.Actor->GetActorLocation()))) Target.BurnFraction=0.f;
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
        // 站在高处的角色在火焰上方，不算踩在燃烧的地板上。
        if(Health>0 && CurrentCell==Burning.Cell && FootHeight<=GridRules::GroundTolerance)
            TakeDamage(Burn(BurnFraction),FDamageEvent(),nullptr,nullptr);
        for(auto& Target:Targets) if(Target.Health>0 && Target.Actor.IsValid()
            && GridRules::Cell(Target.Actor->GetActorLocation())==Burning.Cell && Target.FootHeight<=GridRules::GroundTolerance)
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
    // 驱动「站在火上」的全屏橙色反馈，平滑淡入淡出，避免闪跳。
    const bool bOnFire=Health>0 && FootHeight<=GridRules::GroundTolerance && StillBurning(CurrentCell);
    BurnOverlay=FMath::FInterpTo(BurnOverlay,bOnFire?1.f:0.f,DT,8.f);
}

void AGridPawn::SetupCombatShowcase()
{
#if !UE_BUILD_SHIPPING
    // 供视觉 QA 使用的确定性展示场景；正常游玩永远不会创建它。
    CurrentCell=FIntPoint(1,4); Destination=CurrentCell;
    SetActorLocation(GridRules::Center(CurrentCell,GridRules::ActorOriginHeight));
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
