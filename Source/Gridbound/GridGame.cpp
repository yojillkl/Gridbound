#include "GridGame.h"
#include "GridArt.h"
#include "ProceduralMeshComponent.h"
#include "Modules/ModuleManager.h"
#include "Camera/CameraComponent.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Misc/Paths.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "DrawDebugHelpers.h"
#include "InputCoreTypes.h"

// 本文件承载 AGridPawn 的主干：出生/初始化、伤害、移动合法性、法术施放、火球、
// 土墙、每帧 Tick 以及 HUD。移动、敌人、关卡、环境与美术逻辑分别拆到其他 .cpp。
IMPLEMENT_PRIMARY_GAME_MODULE(FDefaultGameModuleImpl, Gridbound, "Gridbound");

AGridGameMode::AGridGameMode()
{
    DefaultPawnClass=AGridPawn::StaticClass();
    HUDClass=AGridHUD::StaticClass();
}

AGridPawn::AGridPawn()
{
    PrimaryActorTick.bCanEverTick=true;
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    Body=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
    Body->SetupAttachment(RootComponent);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetRelativeScale3D(FVector(.55f,.55f,1.4f));
    Camera=CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(RootComponent);
    Camera->SetRelativeLocation(FVector(0,0,GridRules::EyeHeight-GridRules::ActorOriginHeight));
    Camera->bUsePawnControlRotation=true;
    Camera->FieldOfView=90.f;
    Camera->PostProcessSettings.bOverride_BloomIntensity=true;
    Camera->PostProcessSettings.BloomIntensity=.22f;
    Camera->PostProcessSettings.bOverride_AmbientOcclusionIntensity=true;
    Camera->PostProcessSettings.AmbientOcclusionIntensity=.65f;
    Camera->PostProcessSettings.bOverride_VignetteIntensity=true;
    Camera->PostProcessSettings.VignetteIntensity=.18f;
    Camera->PostProcessSettings.bOverride_AutoExposureBias=true;
    Camera->PostProcessSettings.AutoExposureBias=-.55f;
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    CubeMesh=Cube.Object; SphereMesh=Sphere.Object; BaseMaterial=Mat.Object;
    Body->SetStaticMesh(SphereMesh);
}

void AGridPawn::BeginPlay()
{
    Super::BeginPlay();
    // 通过命令行参数切换模式：默认关卡，Sandbox/展示参数则进入对应的自由场景。
    auto* Mat=UMaterialInstanceDynamic::Create(BaseMaterial,this);
    Mat->SetVectorParameterValue(TEXT("Color"),FLinearColor(0.08f,0.65f,0.95f));
    Body->SetMaterial(0,Mat);
    TerrainMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/M_FantasyPaint.M_FantasyPaint"));
    if(!TerrainMaterial) TerrainMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/M_FacetedTile.M_FacetedTile"));
    if(!TerrainMaterial) TerrainMaterial=BaseMaterial;
    Body->SetVisibility(false);
    Adventurer=GridArt::CreateAdventurer(this,RootComponent,TerrainMaterial,false);
    Adventurer->SetOwnerNoSee(true);
    CastingHands=GridArt::CreateCastingHands(this,Camera,TerrainMaterial);
    bLevelMode=!FParse::Param(FCommandLine::Get(),TEXT("GridboundSandbox"))
        && !FParse::Param(FCommandLine::Get(),TEXT("GridboundCombatShowcase"))
        && !FParse::Param(FCommandLine::Get(),TEXT("GridboundRainShowcase"));
    if(FParse::Param(FCommandLine::Get(),TEXT("GridboundLightningShowcase"))) bLevelMode=false;
    if(bLevelMode) PlayerSpawnCell=FIntPoint(2,10);
    BuildArena(); ResetArena();
    if(auto* PC=Cast<APlayerController>(GetController()))
    {
        PC->SetControlRotation(FRotator(-12,0,0));
        PC->bShowMouseCursor=false;
        PC->SetInputMode(FInputModeGameOnly());
    }
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("GridboundCombatShowcase"))) SetupCombatShowcase();
    if(FParse::Param(FCommandLine::Get(),TEXT("GridboundRainShowcase"))) SetupRainShowcase();
    if(FParse::Param(FCommandLine::Get(),TEXT("GridboundLightningShowcase"))) SetupLightningShowcase();
#endif
}

AActor* AGridPawn::MakeBlock(FVector Location,FVector Scale,FLinearColor Color,bool bCollision)
{
    // 生成一个纯色方块 Actor，作为敌人、土柱、地形等实体的通用视觉/碰撞载体。
    AActor* A=GetWorld()->SpawnActor<AActor>(Location,FRotator::ZeroRotator);
    auto* Mesh=NewObject<UStaticMeshComponent>(A);
    A->SetRootComponent(Mesh); A->AddInstanceComponent(Mesh);
    Mesh->SetStaticMesh(CubeMesh);
    Mesh->SetCollisionEnabled(bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
    Mesh->SetCollisionResponseToAllChannels(ECR_Block);
    auto* Mat=UMaterialInstanceDynamic::Create(BaseMaterial,A);
    Mat->SetVectorParameterValue(TEXT("Color"),Color);
    Mesh->SetMaterial(0,Mat); Mesh->RegisterComponent();
    A->SetActorLocation(Location); A->SetActorScale3D(Scale);
    return A;
}

void AGridPawn::BuildArena()
{
    for(int32 X=0;X<GridRules::BoardSize;++X)
    for(int32 Y=0;Y<GridRules::BoardSize;++Y)
    {
        TerrainTiles.Add(FIntPoint(X,Y),GridArt::CreateTile(GetWorld(),FIntPoint(X,Y),TerrainMaterial));
    }
    GridArt::CreateScenery(GetWorld(),TerrainMaterial);
    auto* Light=GetWorld()->SpawnActor<ADirectionalLight>(FVector(0,0,800),FRotator(-48,-30,0));
    Light->GetLightComponent()->SetIntensity(3.2f);
    Light->GetLightComponent()->SetLightColor(FLinearColor(1.f,.88f,.70f));
    Cast<UDirectionalLightComponent>(Light->GetLightComponent())->SetForwardShadingPriority(10);
    Cast<UDirectionalLightComponent>(Light->GetLightComponent())->SetAtmosphereSunLight(true);
    auto* Fill=GetWorld()->SpawnActor<ADirectionalLight>(FVector(0,0,800),FRotator(-35,150,0));
    Fill->GetLightComponent()->SetIntensity(1.5f);
    Fill->GetLightComponent()->SetLightColor(FLinearColor(.62f,.79f,1.f));
    Cast<UDirectionalLightComponent>(Fill->GetLightComponent())->SetForwardShadingPriority(0);
    Cast<UDirectionalLightComponent>(Fill->GetLightComponent())->SetAtmosphereSunLight(false);
    Fill->GetLightComponent()->SetCastShadows(false);
    GetWorld()->SpawnActor<ASkyAtmosphere>();
    auto* Ambient=GetWorld()->SpawnActor<ASkyLight>(FVector(2300,2300,1600),FRotator::ZeroRotator);
    Ambient->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Ambient->GetLightComponent()->SetIntensity(1.1f);
    Ambient->GetLightComponent()->SkyDistanceThreshold=10000.f;
    Ambient->GetLightComponent()->SetLowerHemisphereColor(FLinearColor(.10f,.115f,.12f));
    Ambient->GetLightComponent()->SetLightColor(FLinearColor(.76f,.86f,1.f));
    Ambient->GetLightComponent()->RecaptureSky();
    auto* Fog=GetWorld()->SpawnActor<AExponentialHeightFog>();
    Fog->GetComponent()->SetFogDensity(.014f);
    Fog->GetComponent()->SetFogHeightFalloff(.2f);
    Fog->GetComponent()->SetStartDistance(1800.f);
    Fog->GetComponent()->SetFogInscatteringColor(FLinearColor(.35f,.46f,.47f));
    // 用一块不发光的球体背景盖住棋盘外缘的下方地平线。
    if(auto* SkyMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/M_SkyBackdrop.M_SkyBackdrop")))
    {
        AActor* Sky=MakeBlock(FVector(1400,1400,0),FVector(1000),FLinearColor::White,false);
        auto* SkyMesh=Cast<UStaticMeshComponent>(Sky->GetRootComponent());
        SkyMesh->SetStaticMesh(SphereMesh);
        SkyMesh->SetMaterial(0,SkyMaterial);
        SkyMesh->SetCastShadow(false);
    }
    UE_LOG(LogTemp,Display,TEXT("GRIDBOUND_ART_READY continuous_terrain=1 obstacles=environmental primaryLightPriority=10 fillLightPriority=0 sensitivity=%.2f"),MouseSensitivity);
}

void AGridPawn::ResetArena()
{
    ResetWizardState();
    for(auto& E:ElectricCells) if(E.Actor.IsValid()) E.Actor->Destroy();
    ElectricCells.Reset();
    for(auto& E:LightningEffects) if(E.Actor.IsValid()) E.Actor->Destroy();
    LightningEffects.Reset(); ConductPlan.Reset(); AimTrace=FSpellTrace(); AimHint.Empty();
    for(auto& Mud:MuddyCells) if(Mud.Actor.IsValid()) Mud.Actor->Destroy();
    MuddyCells.Reset();
    for(auto& Rain:RainEffects) if(Rain.Actor.IsValid()) Rain.Actor->Destroy();
    RainEffects.Reset();
    for(auto& Burning:BurningCells) if(Burning.Actor.IsValid()) Burning.Actor->Destroy();
    BurningCells.Reset();
    for(auto& Tile:TerrainTiles) if(Tile.Value.IsValid())
        if(auto* Mesh=Cast<UPrimitiveComponent>(Tile.Value->GetRootComponent())) Mesh->SetVisibility(true);
    for(TActorIterator<AActor> It(GetWorld());It;++It) if(It->ActorHasTag(TEXT("GridSpellDebris"))) It->Destroy();
    for(auto& F:Fireballs) if(F.Actor.IsValid()) F.Actor->Destroy();
    Fireballs.Reset();
    for(auto& Wall:Walls) if(Wall.Actor.IsValid()) Wall.Actor->Destroy();
    Walls.Reset();
    for(auto& T:Targets) if(T.Actor.IsValid()) T.Actor->Destroy();
    Targets.Reset();
    CurrentCell=PlayerSpawnCell; Destination=CurrentCell;
    SetActorLocation(GridRules::Center(CurrentCell,GridRules::ActorOriginHeight));
    PlayerWorldPosition=GetActorLocation();
    bMoving=false; MoveTime=0; for(float& CD:Cooldowns) CD=0;
    Health=GridRules::MaxHealth; SelectedSkill=INDEX_NONE; bWallAlongX=false;
    FireboltCooldownRemaining=0.f; WallEffectRemaining=0.f; RainEffectRemaining=0.f;
    CastingSkill=INDEX_NONE; SpellCastAnimation=0.f;
    FootHeight=0.f; FallSpeed=0.f; BurnFraction=0.f; bJumping=false;
    JumpBufferRemaining=0.f; CoyoteRemaining=0.f;
    TripRemaining=0.f; KnockbackVelocity=FVector::ZeroVector;
    PendingMoveInput=FIntPoint::ZeroValue;
    bWaitingForMoveChord=false; MoveChordAge=0.f;
    RespawnRemaining=0.f;
    if(!bLevelMode) for(auto Cell:{EnemySpawnCell,FIntPoint(9,7),FIntPoint(10,7)})
    {
        FIntPoint SafeCell;
        if(FindSpawnCell(Cell,false,SafeCell)) SpawnWarrior(SafeCell);
    }
    Feedback=TEXT("法术已就绪");
    if(bLevelMode) ResetLevel();
}

float AGridPawn::TakeDamage(float DamageAmount,const FDamageEvent& DamageEvent,AController* EventInstigator,AActor* DamageCauser)
{
    // 统一的生命扣除入口：非法/非正伤害忽略，扣到 0 触发死亡重生流程。
    if(!FMath::IsFinite(DamageAmount) || DamageAmount<=0.f || Health<=0 || (bLevelMode && bLevelComplete)) return 0.f;
    const int32 Applied=FMath::Min(Health,FMath::CeilToInt(FMath::Min(DamageAmount,float(GridRules::MaxHealth))));
    Health-=Applied;
    DamageFlash=1.f;
    if(Health==0)
    {
        ClearSustainedSpells();
        CastingSkill=INDEX_NONE; SpellCastAnimation=0.f;
        if(bLevelMode) ++LevelDeaths;
        RespawnRemaining=GridRules::PlayerRespawnDelay;
        bMoving=false; bWaitingForMoveChord=false; PendingMoveInput=FIntPoint::ZeroValue;
        Feedback=TEXT("你已倒下，2 秒后重生……");
    }
    return float(Applied);
}

bool AGridPawn::CanEnter(FIntPoint Cell) const
{
    // 玩家能否踏入某格：棋盘内、非阻挡、高差不超步高、且没有被存活敌人占据。
    if(!GridRules::Inside(Cell)) return false;
    if(bLevelMode)
    {
        // 河面阻挡贴地步行，但跳跃时可以从上面越过；悬崖和晶核门禁始终是实心。
        const bool bHoppingRiver=GridArt::TerrainAt(Cell)==GridArt::ETerrain::River && bJumping;
        if(LevelBlocked(Cell) && !bHoppingRiver) return false;
    }
    if(SurfaceHeight(Cell)>FootHeight+GridRules::MaxStepHeight) return false;
    for(const auto& T:Targets) if(T.Health>0 && (T.Cell==Cell || (T.bWalking && T.MoveDestination==Cell))) return false;
    return true;
}

bool AGridPawn::HasLineOfSight(FIntPoint Cell,const AActor* Target) const
{
    // 从镜头中心射线判断目标格/目标角色是否被地形或土柱遮挡。
    FCollisionQueryParams Params; Params.AddIgnoredActor(this);
    if(Target) Params.AddIgnoredActor(Target);
    FHitResult Hit;
    const FVector End=Target?Target->GetActorLocation():GridRules::Center(Cell,SurfaceHeight(Cell)+5.f);
    return !GetWorld()->LineTraceSingleByChannel(Hit,SpellOrigin(),End,ECC_Visibility,Params);
}

void AGridPawn::DrawCell(FIntPoint Cell,FColor Color,float Life) const
{
    const FVector C=GridRules::Center(Cell,SurfaceHeight(Cell)+4);
    const float H=GridRules::CellSize*.48f;
    const FVector Corners[]={C+FVector(-H,-H,0),C+FVector(H,-H,0),C+FVector(H,H,0),C+FVector(-H,H,0)};
    for(int I=0;I<4;++I) DrawDebugLine(GetWorld(),Corners[I],Corners[(I+1)%4],Color,false,Life,0,3.f);
}

void AGridPawn::UpdateAim()
{
    auto* PC=Cast<APlayerController>(GetController());
    if(!PC) { bHasAim=false; bValidAim=false; return; }
    // 镜头方向和法术起点保持一致，不再把很远的射线落点二次换算为另一条路径。
    ComputeSkillAim(PC->GetControlRotation().Vector());
}

void AGridPawn::ComputeSkillAim(FVector Direction)
{
    bHasAim=false; bValidAim=false; AimEnemy=INDEX_NONE;
    WallPlan.Reset(); ConductPlan.Reset(); AimTrace=FSpellTrace(); AimDistance=0;
    AimHint=TEXT("未选择法术"); AimDirection=Direction.GetSafeNormal();
    if(Health<=0 || SelectedSkill<0 || SelectedSkill>=4) return;
    if(SelectedSkill==1)
    {
        AimPoint=SpellOrigin(); bHasAim=bValidAim=true;
        AimHint=TEXT("自身防护 · AC +5 · 持续 6 秒 · 非无敌");
        return;
    }
    if(AimDirection.IsNearlyZero()) return;
    if(SelectedSkill==0)
    {
        AimTrace=TraceSpell(0,AimDirection); AimPoint=AimTrace.End;
        AimDistance=FVector::Dist(SpellOrigin(),AimPoint); bHasAim=bValidAim=true;
        if(!AimTrace.Enemies.IsEmpty()) AimEnemy=AimTrace.Enemies[0];
        AimHint=FString::Printf(TEXT("8d6 火焰 · 敏捷成功半伤 · 爆炸半径 %.0f 米"),GridRules::FireballBlastRadius/100.f);
        if(FVector::Dist(AimPoint,GetActorLocation())<=GridRules::FireballBlastRadius)
            AimHint=TEXT("危险：自身位于爆炸范围内");
    }
    else if(SelectedSkill==2)
    {
        AimDirection=AimDirection.GetSafeNormal2D();
        bHasAim=bValidAim=!AimDirection.IsNearlyZero();
        AimPoint=GetActorLocation()+AimDirection*GridRules::ThunderSize;
        AimDistance=GridRules::ThunderSize; AimTrace.Enemies=ThunderTargets(AimDirection);
        AimHint=FString::Printf(TEXT("%dd8 雷鸣 · 体质成功半伤 / 失败击退 3 米"),CastLevels[2]+1);
    }
    else
    {
        if(PendingCastSkill==3)
        {
            // Revalidate the original targets; changing aim cannot switch a queued enchantment.
            for(int32 N=0;N<PendingCastTargets.Num();++N)
            {
                const int32 I=Targets.IndexOfByPredicate([&](const FTarget& T){
                    return T.Actor==PendingCastTargets[N] && T.Actor.IsValid() && T.Health>0 && T.bHumanoid;
                });
                const bool Valid=I!=INDEX_NONE
                    && FVector::Dist(SpellOrigin(),Targets[I].Actor->GetActorLocation())<=GridRules::HoldRange
                    && SpellVisible(SpellOrigin(),Targets[I].Actor->GetActorLocation(),Targets[I].Actor.Get());
                if(N==0 && !Valid) { AimHint=TEXT("原目标已失效或被遮挡"); return; }
                if(Valid) AimTrace.Enemies.Add(I);
            }
            bHasAim=bValidAim=!AimTrace.Enemies.IsEmpty();
            if(bValidAim) { AimEnemy=AimTrace.Enemies[0]; AimPoint=Targets[AimEnemy].Actor->GetActorLocation(); AimDistance=FVector::Dist(SpellOrigin(),AimPoint); }
            AimHint=TEXT("锁定原目标 · 完成时检查距离与遮挡");
            return;
        }
        // Targeted enchantment stops at the first visible actor, unlike the old piercing beam.
        FCollisionQueryParams Params; Params.AddIgnoredActor(this);
        FHitResult Hit;
        const FVector End=SpellOrigin()+AimDirection*GridRules::HoldRange;
        const bool HitSomething=GetWorld()->LineTraceSingleByChannel(Hit,SpellOrigin(),End,ECC_Visibility,Params);
        AimPoint=HitSomething?Hit.ImpactPoint:End; AimDistance=FVector::Dist(SpellOrigin(),AimPoint); bHasAim=true;
        if(HitSomething) AimEnemy=Targets.IndexOfByPredicate([&](const FTarget& T){
            return T.Health>0 && T.bHumanoid && T.Actor.Get()==Hit.GetActor();
        });
        bValidAim=AimEnemy!=INDEX_NONE;
        AimHint=bValidAim?TEXT("感知豁免 DC 15 · 麻痹 / 专注"):TEXT("需要射程内可见的类人生物目标");
        if(bValidAim)
        {
            AimTrace.Enemies.Add(AimEnemy);
            if(CastLevels[3]==3)
            {
                int32 Second=INDEX_NONE; float Best=MAX_flt;
                for(int32 I=0;I<Targets.Num();++I)
                {
                    const auto& T=Targets[I];
                    if(I==AimEnemy || T.Health<=0 || !T.bHumanoid || !T.Actor.IsValid()) continue;
                    const FVector Location=T.Actor->GetActorLocation();
                    if(FVector::Dist(SpellOrigin(),Location)>GridRules::HoldRange || !SpellVisible(SpellOrigin(),Location,T.Actor.Get())) continue;
                    const float Distance=FVector::DistSquared(Location,Targets[AimEnemy].Actor->GetActorLocation());
                    if(Distance<Best) { Best=Distance; Second=I; }
                }
                if(Second!=INDEX_NONE) AimTrace.Enemies.Add(Second);
                AimHint+=FString::Printf(TEXT(" · 已选 %d / 2 人"),AimTrace.Enemies.Num());
            }
        }
    }
}

void AGridPawn::CastSkill(int32 Skill)
{
    if(Skill<0 || Skill>=4 || Health<=0 || bLevelComplete) return;
    if(Skill==1)
    {
        if(!HasSpellSlot(1)) { Feedback=TEXT("护盾所选环阶法术位不足"); return; }
        const int32 Level=CastLevels[1];
        if(!ActivateShield()) return;
        --SpellSlots[Level-1]; Cooldowns[1]=GridRules::RoundSeconds;
        if(PendingCastSkill==INDEX_NONE) { CastingSkill=1; SpellCastAnimation=GridArt::CastingDuration(1); }
        if(bLevelMode) ++LevelCasts[1];
        return;
    }
    if(PendingCastSkill!=INDEX_NONE || ActionRemaining>0.f) { Feedback=TEXT("施法动作尚未结束"); return; }
    SelectedSkill=Skill; UpdateAim();
    if(!HasSpellSlot(Skill)) { Feedback=FString::Printf(TEXT("%d 环法术位不足"),CastLevels[Skill]); return; }
    if(!bValidAim) { Feedback=AimHint; return; }
    PendingCastSkill=Skill; PendingCastLevel=CastLevels[Skill];
    CastWindupRemaining=GridRules::SpellWindups[Skill];
    PendingCastTargets.Reset();
    if(Skill==3) for(int32 I:AimTrace.Enemies) PendingCastTargets.Add(Targets[I].Actor);
    CastingSkill=Skill; SpellCastAnimation=0.f;
    Feedback=TEXT("正在准备法术");
}

void AGridPawn::CancelSpellCast()
{
    if(PendingCastSkill!=INDEX_NONE) { CastingSkill=INDEX_NONE; SpellCastAnimation=0.f; }
    PendingCastSkill=INDEX_NONE; PendingCastLevel=0; CastWindupRemaining=0.f;
    PendingCastTargets.Reset();
}

void AGridPawn::TickSpellCast(float DeltaSeconds)
{
    if(PendingCastSkill==INDEX_NONE) return;
    if(Health<=0 || bLevelComplete) { CancelSpellCast(); return; }
    CastWindupRemaining=FMath::Max(0.f,CastWindupRemaining-FMath::Max(0.f,DeltaSeconds));
    if(CastWindupRemaining<=0.f) ReleaseSpellCast();
}

void AGridPawn::ReleaseSpellCast()
{
    const int32 Skill=PendingCastSkill,Level=PendingCastLevel;
    if(Skill==INDEX_NONE) return;
    if(Health<=0 || bLevelComplete || Level<1 || Level>3 || SpellSlots[Level-1]<=0)
    { CancelSpellCast(); Feedback=TEXT("施法取消：无法完成或法术位不足"); return; }
    const int32 PreviousSkill=SelectedSkill,PreviousLevel=CastLevels[Skill];
    SelectedSkill=Skill; CastLevels[Skill]=Level;
    UpdateAim();
    CastLevels[Skill]=PreviousLevel; SelectedSkill=PreviousSkill;
    if(!bValidAim) { CancelSpellCast(); Feedback=TEXT("施法取消：目标已失效或被遮挡"); return; }
    const FVector Direction=AimDirection;
    const float Distance=AimDistance;
    const TArray<int32> Enemies=AimTrace.Enemies;
    CancelSpellCast();
    --SpellSlots[Level-1];
    if(Skill==0)
    {
        LaunchFireball(Direction); Fireballs.Last().Remaining=Distance;
        Feedback=TEXT("火球术 · 三环法术位 -1");
    }
    else if(Skill==2) CastThunderwave(Direction,Level);
    else CastHoldPerson(Enemies);
    ActionRemaining=GridRules::SpellActionSeconds; Cooldowns[Skill]=GridRules::SpellActionSeconds;
    CastingSkill=Skill; SpellCastAnimation=GridArt::CastingDuration(Skill);
    GridArt::AnimateCastingHands(CastingHands,SelectedSkill,CastingSkill,SpellCastAnimation,GetWorld()->GetTimeSeconds(),0,DamageFlash);
    if(bLevelMode) ++LevelCasts[Skill];
}
void AGridPawn::LaunchFireball(FVector Direction,bool bCantrip)
{
    // 生成一个沿指定方向飞行的火球实体，命中与射程结算交给 TickFireballs。
    if(Direction.IsNearlyZero()) return;
    AActor* Actor=MakeBlock(SpellOrigin(),FVector::OneVector,FLinearColor(1.f,.18f,.015f),false);
    Cast<UStaticMeshComponent>(Actor->GetRootComponent())->SetVisibility(false);
    GridArt::CreateFireball(Actor,Actor->GetRootComponent(),TerrainMaterial?TerrainMaterial.Get():BaseMaterial.Get());
    Actor->SetActorRotation(Direction.Rotation());
    FFireball Fireball; Fireball.Actor=Actor; Fireball.Direction=Direction.GetSafeNormal();
    Fireball.bCantrip=bCantrip;
    if(bCantrip) Actor->SetActorScale3D(FVector(.45f));
    Fireballs.Add(Fireball);
}

void AGridPawn::TickFireballs(float DeltaSeconds)
{
    // 火球沿直线飞行，用球形扫掠检测命中；耗尽射程或命中即销毁。
    for(int32 I=Fireballs.Num()-1;I>=0;--I)
    {
        auto& Fireball=Fireballs[I];
        if(!Fireball.Actor.IsValid()) { Fireballs.RemoveAtSwap(I); continue; }
        const FVector Start=Fireball.Actor->GetActorLocation();
        const float Travel=FMath::Min(GridRules::FireballSpeed*FMath::Max(0.f,DeltaSeconds),Fireball.Remaining);
        const FVector End=Start+Fireball.Direction*Travel;
        FCollisionQueryParams Params; Params.AddIgnoredActor(this); Params.AddIgnoredActor(Fireball.Actor.Get());
        FHitResult Hit;
        const bool bHit=GetWorld()->SweepSingleByChannel(Hit,Start,End,FQuat::Identity,ECC_Visibility,
            FCollisionShape::MakeSphere(GridRules::FireballRadius),Params);
        Fireball.Remaining-=Travel;
        if(bHit || Fireball.Remaining<=0.f)
        {
            ResolveFireballImpact(Hit.GetActor(),bHit?Hit.Location:End,Fireball.bCantrip);
        }
        if(bHit || Fireball.Remaining<=0.f)
        {
            Fireball.Actor->Destroy(); Fireballs.RemoveAtSwap(I);
        }
        else
        {
            Fireball.Actor->SetActorLocation(End);
            Fireball.Actor->SetActorScale3D(FVector((Fireball.bCantrip?.45f:1.f)*(1.f+.06f*FMath::Sin(Fireball.Remaining*.1f))));
        }
    }
}

bool AGridPawn::CanPlaceWall(FIntPoint CenterCell) const
{
    return !PlaceableWallCells(CenterCell).IsEmpty();
}

bool AGridPawn::PlaceWall(FIntPoint CenterCell)
{
    // 先在生成前算完整份土墙方案：新土柱不能挡住同批的其他土柱。
    const auto Cells=PlaceableWallCells(CenterCell);
    if(Cells.IsEmpty()) return false;
    if(bMoving && (Cells.Contains(CurrentCell) || Cells.Contains(Destination)))
    {
        CurrentCell=GridRules::Cell(GetActorLocation()); Destination=CurrentCell;
        bMoving=false; MoveTime=0; PendingMoveInput=FIntPoint::ZeroValue;
        bWaitingForMoveChord=false; MoveChordAge=0.f;
        SetActorLocation(GridRules::Center(CurrentCell,FootHeight+GridRules::ActorOriginHeight));
    }
    for(auto& T:Targets) if(T.Health>0 && T.bWalking && T.Actor.IsValid()
        && (Cells.Contains(T.Cell) || Cells.Contains(T.MoveDestination)))
    {
        T.Cell=GridRules::Cell(T.Actor->GetActorLocation()); T.MoveDestination=T.Cell;
        T.bWalking=false; T.MoveProgress=0;
        T.bCharging=false; T.ChargeRemaining=0; T.ThinkTime=0;
        T.MoveGoal=T.Actor->GetActorLocation();
    }
    for(const FIntPoint Cell:Cells)
    {
        FWall Wall; Wall.Cell=Cell; Wall.Age=GridRules::WallRiseTime;
        Wall.Actor=MakeBlock(GridRules::Center(Cell,GridRules::WallHeight*.5f),
            FVector(GridRules::CellSize/100.f,GridRules::CellSize/100.f,GridRules::WallHeight/100.f),FLinearColor(.34f,.19f,.075f));
        Cast<UStaticMeshComponent>(Wall.Actor->GetRootComponent())->SetVisibility(false);
        Wall.Visual=GridArt::CreateEarthPillar(Wall.Actor.Get(),Wall.Actor->GetRootComponent(),TerrainMaterial?TerrainMaterial.Get():BaseMaterial.Get(),Cell,Wall.Durability);
        Wall.Visual->SetAbsolute(false,false,true);
        Walls.Add(Wall);
    }
    TickWalls(0.f); UpdateElevation(0.f);
    WallEffectRemaining=GridRules::WallLifetime;
    return true;
}

void AGridPawn::Tick(float DT)
{
    Super::Tick(DT);
    TickCombatReadouts(DT);
    DamageFlash=FMath::Max(0.f,DamageFlash-DT*2.5f);
    SpellCastAnimation=FMath::Max(0.f,SpellCastAnimation-DT);
    TickWalls(DT);
    UpdateElevation(DT);
    TickJumpInput(DT);
    TickPlayerControlEffects(DT);
    TickBurning(DT);
    TickElectricity(DT);
    TickWetTerrain(DT);
    TickMageState(DT);
    TickFireballs(DT);
    TickCombatants(DT);
    if(bLevelMode) TickLevel(DT);
#if !UE_BUILD_SHIPPING
    int32 DicePreview=0;
    if(FParse::Value(FCommandLine::Get(),TEXT("GridboundDicePreview="),DicePreview) && Targets.Num()==1)
    {
        auto& T=Targets[0]; T.StaggerRemaining=1.f;
        static bool bDicePreviewed=false;
        const float Trigger=DicePreview==2?5.88f:5.4f;
        if(!bDicePreviewed && GetWorld()->GetTimeSeconds()>Trigger)
        {
            bDicePreviewed=true;
            CombatRandom.Initialize(177);
            ExplodeFireball(T.Actor->GetActorLocation());
            CastSkill(1);
            UseWarriorSecondWind(0);
            FPendingAttack Attack; Attack.Roll=18; Attack.Bonus=T.AttackBonus; Attack.Damage=9; Attack.Source=T.Actor;
            ResolvePendingAttack(Attack);
        }
    }
    // 可选的艺术截图：等曝光与大气渲染稳定后截取一次。
    int32 WizardPreview=INDEX_NONE;
    if(FParse::Value(FCommandLine::Get(),TEXT("GridboundWizardPreview="),WizardPreview) && Targets.Num()==1)
    {
        auto& T=Targets[0];
        if(GetWorld()->GetTimeSeconds()<5.7f) T.StaggerRemaining=1.f;
        static bool bWizardPreviewed=false;
        if(!bWizardPreviewed && GetWorld()->GetTimeSeconds()>5.7f)
        {
            bWizardPreviewed=true;
            WizardPreview=FMath::Clamp(WizardPreview,0,3);
            if(WizardPreview==2) RespawnPlayer(GridRules::Cell(T.Actor->GetActorLocation())-FIntPoint(2,0));
            if(auto* PreviewController=Cast<APlayerController>(GetController()))
                PreviewController->SetControlRotation((T.Actor->GetActorLocation()-SpellOrigin()).Rotation());
            if(WizardPreview==3) T.WisdomSave=-100;
            CastSkill(WizardPreview); SelectedSkill=WizardPreview;
        }
    }
    static bool bArtCaptured=false;
    if(!bArtCaptured && GetWorld()->GetTimeSeconds()>6.f && FParse::Param(FCommandLine::Get(),TEXT("GridboundScreenshot")))
    {
        FScreenshotRequest::RequestScreenshot(TEXT("GridboundArt.png"),true,false);
        bArtCaptured=true;
    }
#endif
    auto* PC=Cast<APlayerController>(GetController()); if(!PC) return;
    if(PC->WasInputKeyJustPressed(EKeys::F5)) ResetArena();
    if(CastingHands) CastingHands->SetVisibility(Health>0,true);
    if(Health<=0) return;
    float MX,MY; PC->GetInputMouseDelta(MX,MY);
    FRotator Rot=PC->GetControlRotation(); Rot.Yaw+=MX*MouseSensitivity; Rot.Pitch=FMath::Clamp(FRotator::NormalizeAxis(Rot.Pitch)+MY*MouseSensitivity,-85.f,85.f);
    PC->SetControlRotation(Rot);
    Body->SetWorldRotation(FRotator(0,Rot.Yaw,0));
    if(Adventurer) Adventurer->SetWorldRotation(FRotator(0,Rot.Yaw,0));
    for(float& Cooldown:Cooldowns) Cooldown=FMath::Max(0.f,Cooldown-DT);
    if(PC->WasInputKeyJustPressed(EKeys::SpaceBar)) TryJump();
    const auto Active=[PC](FKey Key) { return PC->IsInputKeyDown(Key) || PC->WasInputKeyJustPressed(Key); };
    FVector2D Input(
        PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftY),
        PC->GetInputAnalogKeyState(EKeys::Gamepad_LeftX));
    const FVector2D KeyboardInput(
        float(Active(EKeys::W))-float(Active(EKeys::S)),
        float(Active(EKeys::D))-float(Active(EKeys::A)));
    // 键盘优先；摇杆死区避免漂移覆盖 WASD。
    if(!KeyboardInput.IsNearlyZero()) Input=KeyboardInput;
    else if(Input.SizeSquared()<.04f) Input=FVector2D::ZeroVector;
    const FVector BeforeMove=GetActorLocation();
    MoveContinuous(DT,Input,Rot.Yaw);
    const float Travel=FVector::Dist2D(BeforeMove,GetActorLocation());
    CharacterAnimationTime+=Travel*.025f;
    if(CastingHands)
    {
        GridArt::AnimateCastingHands(CastingHands,SelectedSkill,CastingSkill,SpellCastAnimation,
            GetWorld()->GetTimeSeconds(),DT>0?Travel/DT/MovementSpeed:0,DamageFlash,DT,
            PendingCastSkill!=INDEX_NONE?1.f-CastWindupRemaining/GridRules::SpellWindups[PendingCastSkill]:-1.f);
    }
    if(PC->WasInputKeyJustPressed(EKeys::Q)) SelectSkill(0);
    else if(PC->WasInputKeyJustPressed(EKeys::F)) SelectSkill(3);
    else if(PC->WasInputKeyJustPressed(EKeys::E))
    {
        if(!PendingAttacks.IsEmpty()) CastSkill(1);
        else SelectSkill(1);
    }
    else if(PC->WasInputKeyJustPressed(EKeys::R)) SelectSkill(2);
    if(PC->WasInputKeyJustPressed(EKeys::MouseScrollUp)) ChangeCastLevel(1);
    if(PC->WasInputKeyJustPressed(EKeys::MouseScrollDown)) ChangeCastLevel(-1);
    if(PC->WasInputKeyJustPressed(EKeys::RightMouseButton)) { CancelSpellCast(); SelectedSkill=INDEX_NONE; }
    if(PC->WasInputKeyJustPressed(EKeys::X)) EndConcentration();
    UpdateAim();
    DrawSkillPreview();
    if(PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
    {
        if(SelectedSkill!=INDEX_NONE) CastSkill(SelectedSkill);
    }
#if !UE_BUILD_SHIPPING
    // Optional deterministic pose capture; only activated by an explicit development argument.
    int32 PreviewSkill=INDEX_NONE;
    if(FParse::Value(FCommandLine::Get(),TEXT("GridboundAnimationSkill="),PreviewSkill))
    {
        PreviewSkill=FMath::Clamp(PreviewSkill,0,3);
        float Progress=.18f;
        FParse::Value(FCommandLine::Get(),TEXT("GridboundAnimationProgress="),Progress);
        Progress=FMath::Clamp(Progress,0.f,1.f);
        SelectedSkill=PreviewSkill;
        GridArt::AnimateCastingHands(CastingHands,PreviewSkill,PreviewSkill,GridArt::CastingDuration(PreviewSkill)*(1.f-Progress),0,0,0);
        if(Targets.Num()>0 && Targets[0].Visual.IsValid()) {
            auto* Visual=Targets[0].Visual.Get();
            GridArt::AnimateAdventurer(Visual,0,0,0,0,0);
            GridArt::AnimateWarriorSkill(Visual,PreviewSkill+1,.65f,
                FParse::Param(FCommandLine::Get(),TEXT("GridboundAnimationRelease"))?Progress:-1.f,false,0);
        }
        Feedback=TEXT("动作检查：玩家释放与收招 / 战士技能动作");
    }
#endif
}

void AGridHUD::DrawHUD()
{
    Super::DrawHUD();
    auto* P=Cast<AGridPawn>(GetOwningPawn()); if(!P || !Canvas) return;
    if(!ChineseFont)
    {
        ChineseFont=NewObject<UFont>(this);
        ChineseFont->FontCacheType=EFontCacheType::Runtime;
        ChineseFont->LegacyFontSize=18;
        ChineseFont->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.Add(FTypefaceEntry(
            FName(TEXT("Regular")),FPaths::ProjectContentDir()/TEXT("Fonts/DroidSansFallback.ttf"),EFontHinting::Default,EFontLoadingPolicy::LazyLoad));
    }
    const float W=Canvas->SizeX,H=Canvas->SizeY;
    float S=FMath::Min(W/1280.f,H/720.f);
    const float SceneScale=S;
    // Keep combat readout colors intact while the world flashes on damage.
    if(P->DamageFlash>0.f) DrawRect(FLinearColor(.85f,.04f,.04f,FMath::Min(P->DamageFlash,1.f)*.32f),0,0,W,H);
    if(P->BurnOverlay>0.f) DrawRect(FLinearColor(1.f,.45f,.05f,FMath::Min(P->BurnOverlay,1.f)*.22f),0,0,W,H);
    auto Text=[&](const FString& Value,float X,float Y,FLinearColor Color=FLinearColor::White)
    { DrawText(Value,Color,X*S,Y*S,ChineseFont,S*.82f); };
    auto Frame=[&](float X,float Y,float Width,float Height,FLinearColor Accent) {
        DrawRect(FLinearColor(.012f,.022f,.027f,.88f),X*S,Y*S,Width*S,Height*S);
        DrawRect(FLinearColor(.19f,.23f,.24f,.65f),X*S,Y*S,Width*S,S);
        DrawRect(FLinearColor(.19f,.23f,.24f,.35f),X*S,(Y+Height)*S,Width*S,S);
        for(float EdgeX:{X,X+Width}) {
            DrawLine(EdgeX*S,Y*S,EdgeX*S,(Y+12)*S,Accent,S);
            DrawLine(EdgeX*S,(Y+Height-12)*S,EdgeX*S,(Y+Height)*S,Accent,S);
        }
    };
    DrawCombatReadouts(P,S,W,H);
    if(P->TripRemaining>0.f) Text(TEXT("被绊倒：移动减慢，暂时无法跳跃"),430,H/S-214,FLinearColor(1,.5f,.8f));
    if(P->bLevelComplete)
    {
        DrawRect(FLinearColor(.02f,.06f,.045f,.92f),W*.5f-220*S,H*.5f-65*S,440*S,130*S);
        DrawText(TEXT("决斗胜利 · 战斗大师已倒下"),FLinearColor(.6f,1,.7f),W*.5f-190*S,H*.5f-45*S,ChineseFont,S);
        DrawText(FString::Printf(TEXT("本次用时 %.1f 秒 · 失败 %d 次"),P->LevelSeconds,P->LevelDeaths),
            FLinearColor::White,W*.5f-180*S,H*.5f-8*S,ChineseFont,S*.82f);
        DrawText(TEXT("按 F5 重新挑战"),FLinearColor::White,W*.5f-85*S,H*.5f+28*S,ChineseFont,S*.82f);
    }
    S*=.82f;
    DrawRect(FLinearColor(.015f,.025f,.035f,.8f),20*S,H-68*S,280*S,48*S);
    DrawRect(FLinearColor(.15f,.06f,.06f),34*S,H-36*S,250*S,8*S);
    DrawRect(FLinearColor(.2f,.85f,.45f),34*S,H-36*S,250*S*P->Health/float(GridRules::MaxHealth),8*S);
    DrawText(FString::Printf(TEXT("生命值　%d / %d"),P->Health,GridRules::MaxHealth),FLinearColor::White,34*S,H-63*S,ChineseFont,S*.82f);
    DrawText(P->ReactionRemaining>0.f?FString::Printf(TEXT("反应恢复  %.1f 秒"),P->ReactionRemaining):TEXT("反应就绪"),
        FLinearColor(.65f,.85f,1),34*S,H-98*S,ChineseFont,S*.75f);
    if(!P->PendingAttacks.IsEmpty())
    {
        DrawRect(FLinearColor(.02f,.12f,.17f,.95f),W*.5f-160*S,H*.5f+46*S,320*S,38*S);
        DrawText(FString::Printf(TEXT("E 护盾反应 · %d 环 %s"),P->CastLevels[1],P->HasSpellSlot(1)?TEXT("可用"):TEXT("耗尽")),
            FLinearColor(.4f,.95f,1),W*.5f-146*S,H*.5f+54*S,ChineseFont,S*.82f);
    }
    const FString Names[]={TEXT("Q  火球术"),TEXT("E  护盾术"),TEXT("R  雷鸣波"),TEXT("F  人类定身术")};
    const float ResourceX=W/S-950.f,ResourceY=H/S-116.f;
    Frame(ResourceX,ResourceY,918,30,FLinearColor(.35f,.45f,.5f));
    const FLinearColor SlotColors[]={FLinearColor(.35f,.8f,1.f),FLinearColor(.55f,1.f,.7f),FLinearColor(1.f,.8f,.35f)};
    for(int32 Level=0;Level<3;++Level)
    {
        const float X=ResourceX+12+Level*306.f;
        const bool Selected=P->SelectedSkill>=0 && P->SelectedSkill<GridRules::SkillCount && P->CastLevels[P->SelectedSkill]==Level+1;
        if(Selected) DrawRect(SlotColors[Level],(X-12)*S,ResourceY*S,300*S,2*S);
        Text(FString::Printf(TEXT("%d 环  %d / %d"),Level+1,P->SpellSlots[Level],GridRules::MaxSpellSlots[Level]),X,ResourceY+6,SlotColors[Level]);
        for(int32 I=0;I<GridRules::MaxSpellSlots[Level];++I)
        {
            DrawRect(FLinearColor(.27f,.34f,.38f),(X+114+I*28)*S,(ResourceY+9)*S,20*S,12*S);
            DrawRect(I<P->SpellSlots[Level]?SlotColors[Level]:FLinearColor(.025f,.04f,.045f),
                (X+116+I*28)*S,(ResourceY+11)*S,16*S,8*S);
        }
    }
    const FString Details[]={TEXT("三环 · 8d6 范围火焰"),TEXT("一环 · 自身 / AC +5"),TEXT("一环 · 2d8 / 击退"),TEXT("二环 · 感知 / 专注")};
    const float MaxCD[]={1.f,6.f,1.f,1.f};
    const FLinearColor Colors[]={FLinearColor(1,.55f,.25f),FLinearColor(.3f,.8f,1),FLinearColor(.55f,.9f,1),FLinearColor(1,.45f,.8f)};
    for(int32 I=0;I<GridRules::SkillCount;++I)
    {
        const float X=W/S-950.f+I*232.f,Y=H/S-78.f,SlotW=222.f,SlotH=58.f;
        const bool Active=(I==1 && P->ShieldRemaining>0.f) || (I==3 && P->ConcentrationRemaining>0.f);
        const bool Ready=I==1?P->ReactionRemaining<=0.f:(P->PendingCastSkill==INDEX_NONE && P->ActionRemaining<=0.f),Selected=P->SelectedSkill==I;
        Frame(X,Y,SlotW,SlotH,Selected?Colors[I]:FLinearColor(.27f,.32f,.32f));
        if(Selected) DrawRect(FLinearColor(.06f,.13f,.15f,.75f),(X+1)*S,(Y+1)*S,(SlotW-2)*S,(SlotH-2)*S);
        DrawRect(Colors[I],X*S,Y*S,(Selected?SlotW:30.f)*S,(Selected?3.f:2.f)*S);
        const float Fill=FMath::Clamp(1.f-P->Cooldowns[I]/MaxCD[I],0.f,1.f);
        DrawRect(Colors[I],X*S,(Y+SlotH-3)*S,SlotW*S*Fill,2*S);
        Text(Names[I],X+10,Y+7,Colors[I]);
        Text(P->PendingCastSkill==I?TEXT("吟唱中"):Active?TEXT("生效中"):!P->HasSpellSlot(I)?TEXT("耗尽"):Ready?TEXT("就绪"):TEXT("施法中"),
            X+156,Y+7,Active?Colors[I]:!P->HasSpellSlot(I)?FLinearColor(1,.35f,.3f):Ready?FLinearColor(.65f,1,.75f):FLinearColor(1,.7f,.3f));
        Text(Details[I],X+10,Y+31,FLinearColor(.8f,.86f,.9f));
    }
    FLinearColor Reticle=FLinearColor::White;
    if(P->SelectedSkill>=0 && P->SelectedSkill<GridRules::SkillCount)
    {
        const int32 Skill=P->SelectedSkill;
        Reticle=Skill==1 && P->ShieldRemaining>0.f?Colors[Skill]:
            (!P->bValidAim || !P->HasSpellSlot(Skill))?FLinearColor(1,.3f,.25f):P->Cooldowns[Skill]>0?FLinearColor(1,.7f,.25f):Colors[Skill];
        DrawRect(FLinearColor(.015f,.025f,.035f,.9f),W*.5f-370*S,H-184*S,740*S,58*S);
        const FString Distance=Skill==1?TEXT("自身"):P->bHasAim?FString::Printf(TEXT("%s %.1f 米"),(Skill==0 || Skill==3)?TEXT("路径"):TEXT("落点"),P->AimDistance/100.f):TEXT("未找到落点");
        const FString State=Skill==1 && P->ShieldRemaining>0.f?FString::Printf(TEXT("生效中 %.1f 秒"),P->ShieldRemaining):
            Skill==1 && P->ReactionRemaining>0.f?TEXT("反应尚未恢复"):
            P->PendingCastSkill==Skill?FString::Printf(TEXT("吟唱 %.1f 秒"),P->CastWindupRemaining):
            !P->HasSpellSlot(Skill)?TEXT("法术位耗尽"):!P->bValidAim?TEXT("无法施放"):
            Skill!=1 && (P->ActionRemaining>0.f || P->PendingCastSkill!=INDEX_NONE)?TEXT("动作尚未结束"):TEXT("可施放");
        DrawText(FString::Printf(TEXT("%s · %d 环 · %s · %s"),*Names[Skill],P->CastLevels[Skill],*Distance,*State),
            Reticle,W*.5f-355*S,H-176*S,ChineseFont,S*.75f);
        DrawText(P->AimHint,FLinearColor(.86f,.93f,1),W*.5f-355*S,H-151*S,ChineseFont,S*.75f);
    }
    S=SceneScale;
    DrawRect(Reticle,W*.5f-2*S,H*.5f-2*S,4*S,4*S);
    DrawLine(W*.5f-12*S,H*.5f,W*.5f-6*S,H*.5f,Reticle,1.5f*S);
    DrawLine(W*.5f+6*S,H*.5f,W*.5f+12*S,H*.5f,Reticle,1.5f*S);
    DrawLine(W*.5f,H*.5f-12*S,W*.5f,H*.5f-6*S,Reticle,1.5f*S);
    DrawLine(W*.5f,H*.5f+6*S,W*.5f,H*.5f+12*S,Reticle,1.5f*S);
    for(const auto& T:P->Targets)
    {
        if(T.Health<=0 || !T.Actor.IsValid()) continue;
        FCollisionQueryParams Params; Params.AddIgnoredActor(P); Params.AddIgnoredActor(T.Actor.Get());
        FHitResult Hit;
        if(GetWorld()->LineTraceSingleByChannel(Hit,P->Camera->GetComponentLocation(),T.Actor->GetActorLocation(),ECC_Visibility,Params)) continue;
        FVector2D Screen;
        if(PlayerOwner->ProjectWorldLocationToScreen(T.Actor->GetActorLocation()+FVector(0,0,115),Screen))
        {
            int32 Rows=0;
            for(const auto& Entry:P->CombatReadouts)
                if(Entry.bPlayer?Screen.X>W-370*S:Screen.X<370*S) ++Rows;
            if(Rows>0 && Screen.Y+6*S>H*.25f && Screen.Y<H*.25f+(Rows*112-10)*S) continue;
            if(Screen.X-30*S<300*S && Screen.Y+6*S>H-68*S) continue;
            DrawRect(FLinearColor(.06f,.02f,.02f,.9f),Screen.X-30*S,Screen.Y,60*S,6*S);
            DrawRect(FLinearColor(.9f,.16f,.08f),Screen.X-30*S,Screen.Y,60*S*T.Health/T.MaxHealth,6*S);
        }
    }
}
